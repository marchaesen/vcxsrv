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

#if defined(PIPE_ARCH_X86_64) || defined(PIPE_ARCH_PPC_64) || defined(PIPE_ARCH_AARCH64) || defined(PIPE_ARCH_MIPS64)
#define NUM_QUERIES 5000
#else
#define NUM_QUERIES 500
#endif

struct zink_query_buffer {
   struct list_head list;
   unsigned num_results;
   struct pipe_resource *buffer;
   struct pipe_resource *xfb_buffers[PIPE_MAX_VERTEX_STREAMS - 1];
};

struct zink_query {
   struct threaded_query base;
   enum pipe_query_type type;

   VkQueryPool query_pool;
   VkQueryPool xfb_query_pool[PIPE_MAX_VERTEX_STREAMS - 1]; //stream 0 is in the base pool
   unsigned curr_query, last_start;

   VkQueryType vkqtype;
   unsigned index;
   bool precise;
   bool xfb_running;
   bool xfb_overflow;

   bool active; /* query is considered active by vk */
   bool needs_reset; /* query is considered active by vk and cannot be destroyed */
   bool dead; /* query should be destroyed when its fence finishes */
   bool needs_update; /* query needs to update its qbos */

   struct list_head active_list;

   struct list_head stats_list; /* when active, statistics queries are added to ctx->primitives_generated_queries */
   bool have_gs[NUM_QUERIES]; /* geometry shaders use GEOMETRY_SHADER_PRIMITIVES_BIT */
   bool have_xfb[NUM_QUERIES]; /* xfb was active during this query */

   struct zink_batch_usage *batch_id; //batch that the query was started in

   struct list_head buffers;
   union {
      struct zink_query_buffer *curr_qbo;
      struct pipe_fence_handle *fence; //PIPE_QUERY_GPU_FINISHED
   };

   struct zink_resource *predicate;
   bool predicate_dirty;
};

static void
update_qbo(struct zink_context *ctx, struct zink_query *q);
static void
reset_pool(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q);

static inline unsigned
get_num_results(enum pipe_query_type query_type)
{
   switch (query_type) {
   case PIPE_QUERY_OCCLUSION_COUNTER:
   case PIPE_QUERY_OCCLUSION_PREDICATE:
   case PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE:
   case PIPE_QUERY_TIME_ELAPSED:
   case PIPE_QUERY_TIMESTAMP:
   case PIPE_QUERY_PIPELINE_STATISTICS_SINGLE:
      return 1;
   case PIPE_QUERY_PRIMITIVES_GENERATED:
   case PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE:
   case PIPE_QUERY_SO_OVERFLOW_PREDICATE:
   case PIPE_QUERY_PRIMITIVES_EMITTED:
      return 2;
   default:
      debug_printf("unknown query: %s\n",
                   util_str_query_type(query_type, true));
      unreachable("zink: unknown query type");
   }
}

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
      FALLTHROUGH;
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

static bool
is_bool_query(struct zink_query *query)
{
   return is_so_overflow_query(query) ||
          query->type == PIPE_QUERY_OCCLUSION_PREDICATE ||
          query->type == PIPE_QUERY_OCCLUSION_PREDICATE_CONSERVATIVE ||
          query->type == PIPE_QUERY_GPU_FINISHED;
}

static void
qbo_sync_from_prev(struct zink_context *ctx, struct zink_query *query, unsigned id_offset, unsigned last_start)
{
   assert(id_offset);

   struct zink_query_buffer *prev = list_last_entry(&query->buffers, struct zink_query_buffer, list);
   unsigned result_size = get_num_results(query->type) * sizeof(uint64_t);
   /* this is get_buffer_offset() but without the zink_query object */
   unsigned qbo_offset = last_start * get_num_results(query->type) * sizeof(uint64_t);
   query->curr_query = id_offset;
   query->curr_qbo->num_results = id_offset;
   zink_copy_buffer(ctx, zink_resource(query->curr_qbo->buffer), zink_resource(prev->buffer), 0,
                    qbo_offset,
                    id_offset * result_size);
}

static bool
qbo_append(struct pipe_screen *screen, struct zink_query *query)
{
   if (query->curr_qbo && query->curr_qbo->list.next)
      return true;
   struct zink_query_buffer *qbo = CALLOC_STRUCT(zink_query_buffer);
   if (!qbo)
      return false;
   qbo->buffer = pipe_buffer_create(screen, PIPE_BIND_QUERY_BUFFER,
                                  PIPE_USAGE_STAGING,
                                  /* this is the maximum possible size of the results in a given buffer */
                                  NUM_QUERIES * get_num_results(query->type) * sizeof(uint64_t));
   if (!qbo->buffer)
      goto fail;
   if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED) {
      /* need separate xfb buffer */
      qbo->xfb_buffers[0] = pipe_buffer_create(screen, PIPE_BIND_QUERY_BUFFER,
                                     PIPE_USAGE_STAGING,
                                     /* this is the maximum possible size of the results in a given buffer */
                                     NUM_QUERIES * get_num_results(query->type) * sizeof(uint64_t));
      if (!qbo->xfb_buffers[0])
         goto fail;
   } else if (query->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      /* need to monitor all xfb streams */
      for (unsigned i = 0; i < ARRAY_SIZE(qbo->xfb_buffers); i++) {
         /* need separate xfb buffer */
         qbo->xfb_buffers[i] = pipe_buffer_create(screen, PIPE_BIND_QUERY_BUFFER,
                                        PIPE_USAGE_STAGING,
                                        /* this is the maximum possible size of the results in a given buffer */
                                        NUM_QUERIES * get_num_results(query->type) * sizeof(uint64_t));
         if (!qbo->xfb_buffers[i])
            goto fail;
      }
   }
   list_addtail(&qbo->list, &query->buffers);

   return true;
fail:
   pipe_resource_reference(&qbo->buffer, NULL);
   for (unsigned i = 0; i < ARRAY_SIZE(qbo->xfb_buffers); i++)
      pipe_resource_reference(&qbo->xfb_buffers[i], NULL);
   FREE(qbo);
   return false;
}

static void
destroy_query(struct zink_screen *screen, struct zink_query *query)
{
   assert(zink_screen_usage_check_completion(screen, query->batch_id));
   if (query->query_pool)
      VKSCR(DestroyQueryPool)(screen->dev, query->query_pool, NULL);
   struct zink_query_buffer *qbo, *next;
   LIST_FOR_EACH_ENTRY_SAFE(qbo, next, &query->buffers, list) {
      pipe_resource_reference(&qbo->buffer, NULL);
      for (unsigned i = 0; i < ARRAY_SIZE(qbo->xfb_buffers); i++)
         pipe_resource_reference(&qbo->xfb_buffers[i], NULL);
      FREE(qbo);
   }
   for (unsigned i = 0; i < ARRAY_SIZE(query->xfb_query_pool); i++) {
      if (query->xfb_query_pool[i])
         VKSCR(DestroyQueryPool)(screen->dev, query->xfb_query_pool[i], NULL);
   }
   pipe_resource_reference((struct pipe_resource**)&query->predicate, NULL);
   FREE(query);
}

static void
reset_qbo(struct zink_query *q)
{
   q->curr_qbo = list_first_entry(&q->buffers, struct zink_query_buffer, list);
   q->curr_qbo->num_results = 0;
}

static struct pipe_query *
zink_create_query(struct pipe_context *pctx,
                  unsigned query_type, unsigned index)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = CALLOC_STRUCT(zink_query);
   VkQueryPoolCreateInfo pool_create = {0};

   if (!query)
      return NULL;
   list_inithead(&query->buffers);

   query->index = index;
   query->type = query_type;
   if (query->type == PIPE_QUERY_GPU_FINISHED)
      return (struct pipe_query *)query;
   query->vkqtype = convert_query_type(query_type, &query->precise);
   if (query->vkqtype == -1)
      return NULL;

   assert(!query->precise || query->vkqtype == VK_QUERY_TYPE_OCCLUSION);

   query->curr_query = 0;

   pool_create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
   pool_create.queryType = query->vkqtype;
   pool_create.queryCount = NUM_QUERIES;
   if (query_type == PIPE_QUERY_PRIMITIVES_GENERATED)
     pool_create.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT |
                                      VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT;
   else if (query_type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE)
      pool_create.pipelineStatistics = pipeline_statistic_convert(index);

   VkResult status = VKSCR(CreateQueryPool)(screen->dev, &pool_create, NULL, &query->query_pool);
   if (status != VK_SUCCESS)
      goto fail;
   if (query_type == PIPE_QUERY_PRIMITIVES_GENERATED) {
      /* if xfb is active, we need to use an xfb query, otherwise we need pipeline statistics */
      pool_create.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
      pool_create.queryType = VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT;
      pool_create.queryCount = NUM_QUERIES;

      status = VKSCR(CreateQueryPool)(screen->dev, &pool_create, NULL, &query->xfb_query_pool[0]);
      if (status != VK_SUCCESS)
         goto fail;
   } else if (query_type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      /* need to monitor all xfb streams */
      for (unsigned i = 0; i < ARRAY_SIZE(query->xfb_query_pool); i++) {
         status = VKSCR(CreateQueryPool)(screen->dev, &pool_create, NULL, &query->xfb_query_pool[i]);
         if (status != VK_SUCCESS)
            goto fail;
      }
   }
   if (!qbo_append(pctx->screen, query))
      goto fail;
   struct zink_batch *batch = &zink_context(pctx)->batch;
   batch->has_work = true;
   query->needs_reset = true;
   if (query->type == PIPE_QUERY_TIMESTAMP) {
      query->active = true;
      /* defer pool reset until end_query since we're guaranteed to be threadsafe then */
      reset_qbo(query);
   }
   return (struct pipe_query *)query;
fail:
   destroy_query(screen, query);
   return NULL;
}

static void
zink_destroy_query(struct pipe_context *pctx,
                   struct pipe_query *q)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_query *query = (struct zink_query *)q;

   /* only destroy if this query isn't active on any batches,
    * otherwise just mark dead and wait
    */
   if (query->batch_id) {
      p_atomic_set(&query->dead, true);
      return;
   }

   destroy_query(screen, query);
}

void
zink_prune_query(struct zink_screen *screen, struct zink_batch_state *bs, struct zink_query *query)
{
   if (!zink_batch_usage_matches(query->batch_id, bs))
      return;
   query->batch_id = NULL;
   if (p_atomic_read(&query->dead))
      destroy_query(screen, query);
}

static void
check_query_results(struct zink_query *query, union pipe_query_result *result,
                    int num_results, uint64_t *results, uint64_t *xfb_results)
{
   uint64_t last_val = 0;
   int result_size = get_num_results(query->type);
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
   unsigned flags = PIPE_MAP_READ;

   if (!wait)
      flags |= PIPE_MAP_DONTBLOCK;
   if (query->base.flushed)
      /* this is not a context-safe operation; ensure map doesn't use slab alloc */
      flags |= PIPE_MAP_THREAD_SAFE;

   util_query_clear_result(result, query->type);

   int num_results = query->curr_query - query->last_start;
   int result_size = get_num_results(query->type) * sizeof(uint64_t);

   struct zink_query_buffer *qbo;
   struct pipe_transfer *xfer;
   LIST_FOR_EACH_ENTRY(qbo, &query->buffers, list) {
      uint64_t *xfb_results = NULL;
      uint64_t *results;
      bool is_timestamp = query->type == PIPE_QUERY_TIMESTAMP || query->type == PIPE_QUERY_TIMESTAMP_DISJOINT;
      results = pipe_buffer_map_range(pctx, qbo->buffer, 0,
                                      (is_timestamp ? 1 : qbo->num_results) * result_size, flags, &xfer);
      if (!results) {
         if (wait)
            debug_printf("zink: qbo read failed!");
         return false;
      }
      struct pipe_transfer *xfb_xfer = NULL;
      if (query->type == PIPE_QUERY_PRIMITIVES_GENERATED) {
         xfb_results = pipe_buffer_map_range(pctx, qbo->xfb_buffers[0], 0,
                                         qbo->num_results * result_size, flags, &xfb_xfer);
         if (!xfb_results) {
            if (wait)
               debug_printf("zink: xfb qbo read failed!");
            pipe_buffer_unmap(pctx, xfer);
            return false;
         }
      }
      check_query_results(query, result, is_timestamp ? 1 : qbo->num_results, results, xfb_results);
      pipe_buffer_unmap(pctx, xfer);
      if (xfb_xfer)
         pipe_buffer_unmap(pctx, xfb_xfer);
      if (query->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
         for (unsigned i = 0; i < ARRAY_SIZE(qbo->xfb_buffers) && !result->b; i++) {
            uint64_t *results = pipe_buffer_map_range(pctx, qbo->xfb_buffers[i],
                                              0,
                                              qbo->num_results * result_size, flags, &xfer);
            if (!results) {
               if (wait)
                  debug_printf("zink: qbo read failed!");
               return false;
            }
            check_query_results(query, result, num_results, results, xfb_results);
            pipe_buffer_unmap(pctx, xfer);
         }
         /* if overflow is detected we can stop */
         if (result->b)
            break;
      }
   }

   if (is_time_query(query))
      timestamp_to_nanoseconds(screen, &result->u64);

   return true;
}

static void
force_cpu_read(struct zink_context *ctx, struct pipe_query *pquery, enum pipe_query_value_type result_type, struct pipe_resource *pres, unsigned offset)
{
   struct pipe_context *pctx = &ctx->base;
   unsigned result_size = result_type <= PIPE_QUERY_TYPE_U32 ? sizeof(uint32_t) : sizeof(uint64_t);
   struct zink_query *query = (struct zink_query*)pquery;
   union pipe_query_result result;

   if (query->needs_update)
      update_qbo(ctx, query);

   bool success = get_query_result(pctx, pquery, true, &result);
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
      if (is_bool_query(query))
         u32 = result.b;
      else
         u32 = MIN2(limit, result.u64);
      pipe_buffer_write(pctx, pres, offset, result_size, &u32);
   } else {
      uint64_t u64;
      if (is_bool_query(query))
         u64 = result.b;
      else
         u64 = result.u64;
      pipe_buffer_write(pctx, pres, offset, result_size, &u64);
   }
}

static void
copy_pool_results_to_buffer(struct zink_context *ctx, struct zink_query *query, VkQueryPool pool,
                            unsigned query_id, struct zink_resource *res, unsigned offset,
                            int num_results, VkQueryResultFlags flags)
{
   struct zink_batch *batch = &ctx->batch;
   unsigned type_size = (flags & VK_QUERY_RESULT_64_BIT) ? sizeof(uint64_t) : sizeof(uint32_t);
   unsigned base_result_size = get_num_results(query->type) * type_size;
   unsigned result_size = base_result_size * num_results;
   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
      result_size += type_size;
   zink_batch_no_rp(ctx);
   /* if it's a single query that doesn't need special handling, we can copy it and be done */
   zink_batch_reference_resource_rw(batch, res, true);
   zink_resource_buffer_barrier(ctx, res, VK_ACCESS_TRANSFER_WRITE_BIT, 0);
   util_range_add(&res->base.b, &res->valid_buffer_range, offset, offset + result_size);
   assert(query_id < NUM_QUERIES);
   VKCTX(CmdCopyQueryPoolResults)(batch->state->cmdbuf, pool, query_id, num_results, res->obj->buffer,
                             offset, 0, flags);
}

static void
copy_results_to_buffer(struct zink_context *ctx, struct zink_query *query, struct zink_resource *res, unsigned offset, int num_results, VkQueryResultFlags flags)
{
   copy_pool_results_to_buffer(ctx, query, query->query_pool, query->last_start, res, offset, num_results, flags);
}

static void
reset_pool(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   unsigned last_start = q->last_start;
   unsigned id_offset = q->curr_query - q->last_start;
   /* This command must only be called outside of a render pass instance
    *
    * - vkCmdResetQueryPool spec
    */
   zink_batch_no_rp(ctx);
   if (q->needs_update)
      update_qbo(ctx, q);

   VKCTX(CmdResetQueryPool)(batch->state->cmdbuf, q->query_pool, 0, NUM_QUERIES);
   if (q->type == PIPE_QUERY_PRIMITIVES_GENERATED)
      VKCTX(CmdResetQueryPool)(batch->state->cmdbuf, q->xfb_query_pool[0], 0, NUM_QUERIES);
   else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++)
         VKCTX(CmdResetQueryPool)(batch->state->cmdbuf, q->xfb_query_pool[i], 0, NUM_QUERIES);
   }
   memset(q->have_gs, 0, sizeof(q->have_gs));
   memset(q->have_xfb, 0, sizeof(q->have_xfb));
   q->last_start = q->curr_query = 0;
   q->needs_reset = false;
   /* create new qbo for non-timestamp queries:
    * timestamp queries should never need more than 2 entries in the qbo
    */
   if (q->type == PIPE_QUERY_TIMESTAMP)
      return;
   if (qbo_append(ctx->base.screen, q))
      reset_qbo(q);
   else
      debug_printf("zink: qbo alloc failed on reset!");
   if (id_offset)
      qbo_sync_from_prev(ctx, q, id_offset, last_start);
}

static inline unsigned
get_buffer_offset(struct zink_query *q, struct pipe_resource *pres, unsigned query_id)
{
   return (query_id - q->last_start) * get_num_results(q->type) * sizeof(uint64_t);
}

static void
update_qbo(struct zink_context *ctx, struct zink_query *q)
{
   struct zink_query_buffer *qbo = q->curr_qbo;
   unsigned offset = 0;
   uint32_t query_id = q->curr_query - 1;
   bool is_timestamp = q->type == PIPE_QUERY_TIMESTAMP || q->type == PIPE_QUERY_TIMESTAMP_DISJOINT;
   /* timestamp queries just write to offset 0 always */
   if (!is_timestamp)
      offset = get_buffer_offset(q, qbo->buffer, query_id);
   copy_pool_results_to_buffer(ctx, q, q->query_pool, query_id, zink_resource(qbo->buffer),
                          offset,
                          1, VK_QUERY_RESULT_64_BIT);

   if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED ||
       q->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
       q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE) {
      copy_pool_results_to_buffer(ctx, q,
                                  q->xfb_query_pool[0] ? q->xfb_query_pool[0] : q->query_pool,
                                  query_id,
                                  zink_resource(qbo->xfb_buffers[0] ? qbo->xfb_buffers[0] : qbo->buffer),
                             get_buffer_offset(q, qbo->xfb_buffers[0] ? qbo->xfb_buffers[0] : qbo->buffer, query_id),
                             1, VK_QUERY_RESULT_64_BIT);
   }

   else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++) {
         copy_pool_results_to_buffer(ctx, q, q->xfb_query_pool[i], query_id, zink_resource(qbo->xfb_buffers[i]),
                                get_buffer_offset(q, qbo->xfb_buffers[i], query_id),
                                1, VK_QUERY_RESULT_64_BIT);
      }
   }

   if (!is_timestamp)
      q->curr_qbo->num_results++;
   q->needs_update = false;
}

static void
begin_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   VkQueryControlFlags flags = 0;

   q->predicate_dirty = true;
   if (q->needs_reset)
      reset_pool(ctx, batch, q);
   assert(q->curr_query < NUM_QUERIES);
   q->active = true;
   batch->has_work = true;
   if (q->type == PIPE_QUERY_TIME_ELAPSED) {
      VKCTX(CmdWriteTimestamp)(batch->state->cmdbuf, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, q->query_pool, q->curr_query);
      q->curr_query++;
      update_qbo(ctx, q);
      zink_batch_usage_set(&q->batch_id, batch->state);
      _mesa_set_add(batch->state->active_queries, q);
   }
   /* ignore the rest of begin_query for timestamps */
   if (is_time_query(q))
      return;
   if (q->precise)
      flags |= VK_QUERY_CONTROL_PRECISE_BIT;
   if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED ||
       q->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
       q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE) {
      VKCTX(CmdBeginQueryIndexedEXT)(batch->state->cmdbuf,
                                     q->xfb_query_pool[0] ? q->xfb_query_pool[0] : q->query_pool,
                                     q->curr_query,
                                     flags,
                                     q->index);
      q->xfb_running = true;
   } else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      VKCTX(CmdBeginQueryIndexedEXT)(batch->state->cmdbuf,
                                     q->query_pool,
                                     q->curr_query,
                                     flags,
                                     0);
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++)
         VKCTX(CmdBeginQueryIndexedEXT)(batch->state->cmdbuf,
                                        q->xfb_query_pool[i],
                                        q->curr_query,
                                        flags,
                                        i + 1);
      q->xfb_running = true;
   }
   if (q->vkqtype != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT)
      VKCTX(CmdBeginQuery)(batch->state->cmdbuf, q->query_pool, q->curr_query, flags);
   if (needs_stats_list(q))
      list_addtail(&q->stats_list, &ctx->primitives_generated_queries);
   zink_batch_usage_set(&q->batch_id, batch->state);
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
   /* drop all past results */
   reset_qbo(query);

   begin_query(ctx, batch, query);

   return true;
}

static void
update_query_id(struct zink_context *ctx, struct zink_query *q)
{
   if (++q->curr_query == NUM_QUERIES) {
      /* always reset on start; this ensures we can actually submit the batch that the current query is on */
      q->needs_reset = true;
   }
   ctx->batch.has_work = true;

   if (ctx->batch.in_rp)
      q->needs_update = true;
   else
      update_qbo(ctx, q);
}

static void
end_query(struct zink_context *ctx, struct zink_batch *batch, struct zink_query *q)
{
   ASSERTED struct zink_query_buffer *qbo = q->curr_qbo;
   assert(qbo);
   assert(!is_time_query(q));
   q->active = false;
   if (q->type == PIPE_QUERY_PRIMITIVES_EMITTED ||
            q->type == PIPE_QUERY_PRIMITIVES_GENERATED ||
            q->type == PIPE_QUERY_SO_OVERFLOW_PREDICATE) {
      VKCTX(CmdEndQueryIndexedEXT)(batch->state->cmdbuf,
                                   q->xfb_query_pool[0] ? q->xfb_query_pool[0] : q->query_pool,
                                   q->curr_query, q->index);
   }

   else if (q->type == PIPE_QUERY_SO_OVERFLOW_ANY_PREDICATE) {
      VKCTX(CmdEndQueryIndexedEXT)(batch->state->cmdbuf, q->query_pool, q->curr_query, 0);
      for (unsigned i = 0; i < ARRAY_SIZE(q->xfb_query_pool); i++) {
         VKCTX(CmdEndQueryIndexedEXT)(batch->state->cmdbuf, q->xfb_query_pool[i], q->curr_query, i + 1);
      }
   }
   if (q->vkqtype != VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT && !is_time_query(q))
      VKCTX(CmdEndQuery)(batch->state->cmdbuf, q->query_pool, q->curr_query);

   if (needs_stats_list(q))
      list_delinit(&q->stats_list);

   update_query_id(ctx, q);
}

static bool
zink_end_query(struct pipe_context *pctx,
               struct pipe_query *q)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_query *query = (struct zink_query *)q;
   struct zink_batch *batch = &ctx->batch;

   if (query->type == PIPE_QUERY_GPU_FINISHED) {
      pctx->flush(pctx, &query->fence, PIPE_FLUSH_DEFERRED);
      return true;
   }

   /* FIXME: this can be called from a thread, but it needs to write to the cmdbuf */
   threaded_context_unwrap_sync(pctx);

   if (needs_stats_list(query))
      list_delinit(&query->stats_list);
   if (is_time_query(query)) {
      if (query->needs_reset)
         reset_pool(ctx, batch, query);
      VKCTX(CmdWriteTimestamp)(batch->state->cmdbuf, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          query->query_pool, query->curr_query);
      zink_batch_usage_set(&query->batch_id, batch->state);
      _mesa_set_add(batch->state->active_queries, query);
      update_query_id(ctx, query);
   } else if (query->active)
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

   if (query->type == PIPE_QUERY_GPU_FINISHED) {
      struct pipe_screen *screen = pctx->screen;

      result->b = screen->fence_finish(screen, query->base.flushed ? NULL : pctx,
                                        query->fence, wait ? PIPE_TIMEOUT_INFINITE : 0);
      return result->b;
   }

   if (query->needs_update)
      update_qbo(ctx, query);

   if (zink_batch_usage_is_unflushed(query->batch_id)) {
      if (!threaded_query(q)->flushed)
         pctx->flush(pctx, NULL, 0);
      if (!wait)
         return false;
   } else if (!threaded_query(q)->flushed &&
              /* timeline drivers can wait during buffer map */
              !zink_screen(pctx->screen)->info.have_KHR_timeline_semaphore)
      zink_batch_usage_check_completion(ctx, query->batch_id);

   return get_query_result(pctx, q, wait, result);
}

void
zink_suspend_queries(struct zink_context *ctx, struct zink_batch *batch)
{
   set_foreach(batch->state->active_queries, entry) {
      struct zink_query *query = (void*)entry->key;
      /* if a query isn't active here then we don't need to reactivate it on the next batch */
      if (query->active && !is_time_query(query)) {
         end_query(ctx, batch, query);
         /* the fence is going to steal the set off the batch, so we have to copy
          * the active queries onto a list
          */
         list_addtail(&query->active_list, &ctx->suspended_queries);
      }
      if (query->needs_update)
         update_qbo(ctx, query);
      if (query->last_start && query->curr_query > NUM_QUERIES / 2)
         reset_pool(ctx, batch, query);
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
   if (unlikely(!zink_screen(ctx->base.screen)->info.have_EXT_conditional_rendering))
      return;
   struct zink_batch *batch = &ctx->batch;
   VkConditionalRenderingFlagsEXT begin_flags = 0;
   if (ctx->render_condition.inverted)
      begin_flags = VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   VkConditionalRenderingBeginInfoEXT begin_info = {0};
   begin_info.sType = VK_STRUCTURE_TYPE_CONDITIONAL_RENDERING_BEGIN_INFO_EXT;
   begin_info.buffer = ctx->render_condition.query->predicate->obj->buffer;
   begin_info.flags = begin_flags;
   VKCTX(CmdBeginConditionalRenderingEXT)(batch->state->cmdbuf, &begin_info);
   zink_batch_reference_resource_rw(batch, ctx->render_condition.query->predicate, false);
}

void
zink_stop_conditional_render(struct zink_context *ctx)
{
   struct zink_batch *batch = &ctx->batch;
   zink_clear_apply_conditionals(ctx);
   if (unlikely(!zink_screen(ctx->base.screen)->info.have_EXT_conditional_rendering))
      return;
   VKCTX(CmdEndConditionalRenderingEXT)(batch->state->cmdbuf);
}

bool
zink_check_conditional_render(struct zink_context *ctx)
{
   if (!ctx->render_condition_active)
      return true;
   assert(ctx->render_condition.query);

   union pipe_query_result result;
   zink_get_query_result(&ctx->base, (struct pipe_query*)ctx->render_condition.query, true, &result);
   return is_bool_query(ctx->render_condition.query) ?
          ctx->render_condition.inverted != result.b :
          ctx->render_condition.inverted != !!result.u64;
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
         force_cpu_read(ctx, pquery, PIPE_QUERY_TYPE_U32, &res->base.b, 0);
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

   if (index == -1) {
      /* VK_QUERY_RESULT_WITH_AVAILABILITY_BIT will ALWAYS write some kind of result data
       * in addition to the availability result, which is a problem if we're just trying to get availability data
       *
       * if we know that there's no valid buffer data in the preceding buffer range, then we can just
       * stomp on it with a glorious queued buffer copy instead of forcing a stall to manually write to the
       * buffer
       */

      VkQueryResultFlags flag = is_time_query(query) ? 0 : VK_QUERY_RESULT_PARTIAL_BIT;
      if (zink_batch_usage_check_completion(ctx, query->batch_id)) {
         uint64_t u64[2] = {0};
         if (VKCTX(GetQueryPoolResults)(screen->dev, query->query_pool, query_id, 1, 2 * result_size, u64,
                                   0, size_flags | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | flag) == VK_SUCCESS) {
            pipe_buffer_write(pctx, pres, offset, result_size, (unsigned char*)u64 + result_size);
            return;
         }
      }
      struct pipe_resource *staging = pipe_buffer_create(pctx->screen, 0, PIPE_USAGE_STAGING, result_size * 2);
      copy_results_to_buffer(ctx, query, zink_resource(staging), 0, 1, size_flags | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | flag);
      zink_copy_buffer(ctx, res, zink_resource(staging), offset, result_size, result_size);
      pipe_resource_reference(&staging, NULL);
      return;
   }

   if (!is_time_query(query) && !is_bool_query(query)) {
      if (num_queries == 1 && query->type != PIPE_QUERY_PRIMITIVES_GENERATED &&
                              query->type != PIPE_QUERY_PRIMITIVES_EMITTED &&
                              !is_bool_query(query)) {
         if (size_flags == VK_QUERY_RESULT_64_BIT) {
            if (query->needs_update)
               update_qbo(ctx, query);
            /* internal qbo always writes 64bit value so we can just direct copy */
            zink_copy_buffer(ctx, res, zink_resource(query->curr_qbo->buffer), offset,
                             get_buffer_offset(query, query->curr_qbo->buffer, query->last_start),
                             result_size);
         } else
            /* have to do a new copy for 32bit */
            copy_results_to_buffer(ctx, query, res, offset, 1, size_flags);
         return;
      }
   }

   /* TODO: use CS to aggregate results */

   /* unfortunately, there's no way to accumulate results from multiple queries on the gpu without either
    * clobbering all but the last result or writing the results sequentially, so we have to manually write the result
    */
   force_cpu_read(ctx, pquery, result_type, pres, offset);
}

static uint64_t
zink_get_timestamp(struct pipe_context *pctx)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   uint64_t timestamp, deviation;
   assert(screen->info.have_EXT_calibrated_timestamps);
   VkCalibratedTimestampInfoEXT cti = {0};
   cti.sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_EXT;
   cti.timeDomain = VK_TIME_DOMAIN_DEVICE_EXT;
   VKSCR(GetCalibratedTimestampsEXT)(screen->dev, 1, &cti, &timestamp, &deviation);
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
