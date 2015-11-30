/*
 * Copyright (C) 2013 Christoph Bumiller
 * Copyright (C) 2015 Samuel Pitoiset
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * Performance monitoring counters interface to gallium.
 */

#include "st_debug.h"
#include "st_context.h"
#include "st_cb_bitmap.h"
#include "st_cb_perfmon.h"

#include "util/bitset.h"

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_memory.h"

static bool
init_perf_monitor(struct gl_context *ctx, struct gl_perf_monitor_object *m)
{
   struct st_context *st = st_context(ctx);
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st->pipe;
   unsigned *batch = NULL;
   unsigned num_active_counters = 0;
   unsigned max_batch_counters = 0;
   unsigned num_batch_counters = 0;
   int gid, cid;

   st_flush_bitmap_cache(st);

   /* Determine the number of active counters. */
   for (gid = 0; gid < ctx->PerfMonitor.NumGroups; gid++) {
      const struct gl_perf_monitor_group *g = &ctx->PerfMonitor.Groups[gid];
      const struct st_perf_monitor_group *stg = &st->perfmon[gid];

      if (m->ActiveGroups[gid] > g->MaxActiveCounters) {
         /* Maximum number of counters reached. Cannot start the session. */
         if (ST_DEBUG & DEBUG_MESA) {
            debug_printf("Maximum number of counters reached. "
                         "Cannot start the session!\n");
         }
         return false;
      }

      num_active_counters += m->ActiveGroups[gid];
      if (stg->has_batch)
         max_batch_counters += m->ActiveGroups[gid];
   }

   if (!num_active_counters)
      return true;

   stm->active_counters = CALLOC(num_active_counters,
                                 sizeof(*stm->active_counters));
   if (!stm->active_counters)
      return false;

   if (max_batch_counters) {
      batch = CALLOC(max_batch_counters, sizeof(*batch));
      if (!batch)
         return false;
   }

   /* Create a query for each active counter. */
   for (gid = 0; gid < ctx->PerfMonitor.NumGroups; gid++) {
      const struct gl_perf_monitor_group *g = &ctx->PerfMonitor.Groups[gid];
      const struct st_perf_monitor_group *stg = &st->perfmon[gid];
      BITSET_WORD tmp;

      BITSET_FOREACH_SET(cid, tmp, m->ActiveCounters[gid], g->NumCounters) {
         const struct st_perf_monitor_counter *stc = &stg->counters[cid];
         struct st_perf_counter_object *cntr =
            &stm->active_counters[stm->num_active_counters];

         cntr->id       = cid;
         cntr->group_id = gid;
         if (stc->flags & PIPE_DRIVER_QUERY_FLAG_BATCH) {
            cntr->batch_index = num_batch_counters;
            batch[num_batch_counters++] = stc->query_type;
         } else {
            cntr->query = pipe->create_query(pipe, stc->query_type, 0);
            if (!cntr->query)
               goto fail;
         }
         ++stm->num_active_counters;
      }
   }

   /* Create the batch query. */
   if (num_batch_counters) {
      stm->batch_query = pipe->create_batch_query(pipe, num_batch_counters,
                                                  batch);
      stm->batch_result = CALLOC(num_batch_counters, sizeof(stm->batch_result->batch[0]));
      if (!stm->batch_query || !stm->batch_result)
         goto fail;
   }

   FREE(batch);
   return true;

fail:
   FREE(batch);
   return false;
}

static void
reset_perf_monitor(struct st_perf_monitor_object *stm,
                   struct pipe_context *pipe)
{
   unsigned i;

   for (i = 0; i < stm->num_active_counters; ++i) {
      struct pipe_query *query = stm->active_counters[i].query;
      if (query)
         pipe->destroy_query(pipe, query);
   }
   FREE(stm->active_counters);
   stm->active_counters = NULL;
   stm->num_active_counters = 0;

   if (stm->batch_query) {
      pipe->destroy_query(pipe, stm->batch_query);
      stm->batch_query = NULL;
   }
   FREE(stm->batch_result);
   stm->batch_result = NULL;
}

static struct gl_perf_monitor_object *
st_NewPerfMonitor(struct gl_context *ctx)
{
   struct st_perf_monitor_object *stq = ST_CALLOC_STRUCT(st_perf_monitor_object);
   if (stq)
      return &stq->base;
   return NULL;
}

static void
st_DeletePerfMonitor(struct gl_context *ctx, struct gl_perf_monitor_object *m)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st_context(ctx)->pipe;

   reset_perf_monitor(stm, pipe);
   FREE(stm);
}

static GLboolean
st_BeginPerfMonitor(struct gl_context *ctx, struct gl_perf_monitor_object *m)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st_context(ctx)->pipe;
   unsigned i;

   if (!stm->num_active_counters) {
      /* Create a query for each active counter before starting
       * a new monitoring session. */
      if (!init_perf_monitor(ctx, m))
         goto fail;
   }

   /* Start the query for each active counter. */
   for (i = 0; i < stm->num_active_counters; ++i) {
      struct pipe_query *query = stm->active_counters[i].query;
      if (query && !pipe->begin_query(pipe, query))
          goto fail;
   }

   if (stm->batch_query && !pipe->begin_query(pipe, stm->batch_query))
      goto fail;

   return true;

fail:
   /* Failed to start the monitoring session. */
   reset_perf_monitor(stm, pipe);
   return false;
}

static void
st_EndPerfMonitor(struct gl_context *ctx, struct gl_perf_monitor_object *m)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st_context(ctx)->pipe;
   unsigned i;

   /* Stop the query for each active counter. */
   for (i = 0; i < stm->num_active_counters; ++i) {
      struct pipe_query *query = stm->active_counters[i].query;
      if (query)
         pipe->end_query(pipe, query);
   }

   if (stm->batch_query)
      pipe->end_query(pipe, stm->batch_query);
}

static void
st_ResetPerfMonitor(struct gl_context *ctx, struct gl_perf_monitor_object *m)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st_context(ctx)->pipe;

   if (!m->Ended)
      st_EndPerfMonitor(ctx, m);

   reset_perf_monitor(stm, pipe);

   if (m->Active)
      st_BeginPerfMonitor(ctx, m);
}

static GLboolean
st_IsPerfMonitorResultAvailable(struct gl_context *ctx,
                                struct gl_perf_monitor_object *m)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st_context(ctx)->pipe;
   unsigned i;

   if (!stm->num_active_counters)
      return false;

   /* The result of a monitoring session is only available if the query of
    * each active counter is idle. */
   for (i = 0; i < stm->num_active_counters; ++i) {
      struct pipe_query *query = stm->active_counters[i].query;
      union pipe_query_result result;
      if (query && !pipe->get_query_result(pipe, query, FALSE, &result)) {
         /* The query is busy. */
         return false;
      }
   }

   if (stm->batch_query &&
       !pipe->get_query_result(pipe, stm->batch_query, FALSE, stm->batch_result))
      return false;

   return true;
}

static void
st_GetPerfMonitorResult(struct gl_context *ctx,
                        struct gl_perf_monitor_object *m,
                        GLsizei dataSize,
                        GLuint *data,
                        GLint *bytesWritten)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_context *pipe = st_context(ctx)->pipe;
   unsigned i;

   /* Copy data to the supplied array (data).
    *
    * The output data format is: <group ID, counter ID, value> for each
    * active counter. The API allows counters to appear in any order.
    */
   GLsizei offset = 0;
   bool have_batch_query = false;

   if (stm->batch_query)
      have_batch_query = pipe->get_query_result(pipe, stm->batch_query, TRUE,
                                                stm->batch_result);

   /* Read query results for each active counter. */
   for (i = 0; i < stm->num_active_counters; ++i) {
      struct st_perf_counter_object *cntr = &stm->active_counters[i];
      union pipe_query_result result = { 0 };
      int gid, cid;
      GLenum type;

      cid  = cntr->id;
      gid  = cntr->group_id;
      type = ctx->PerfMonitor.Groups[gid].Counters[cid].Type;

      if (cntr->query) {
         if (!pipe->get_query_result(pipe, cntr->query, TRUE, &result))
            continue;
      } else {
         if (!have_batch_query)
            continue;
         result.batch[0] = stm->batch_result->batch[cntr->batch_index];
      }

      data[offset++] = gid;
      data[offset++] = cid;
      switch (type) {
      case GL_UNSIGNED_INT64_AMD:
         *(uint64_t *)&data[offset] = result.u64;
         offset += sizeof(uint64_t) / sizeof(GLuint);
         break;
      case GL_UNSIGNED_INT:
         *(uint32_t *)&data[offset] = result.u32;
         offset += sizeof(uint32_t) / sizeof(GLuint);
         break;
      case GL_FLOAT:
      case GL_PERCENTAGE_AMD:
         *(GLfloat *)&data[offset] = result.f;
         offset += sizeof(GLfloat) / sizeof(GLuint);
         break;
      }
   }

   if (bytesWritten)
      *bytesWritten = offset * sizeof(GLuint);
}


bool
st_have_perfmon(struct st_context *st)
{
   struct pipe_screen *screen = st->pipe->screen;

   if (!screen->get_driver_query_info || !screen->get_driver_query_group_info)
      return false;

   return screen->get_driver_query_group_info(screen, 0, NULL) != 0;
}

static void
st_InitPerfMonitorGroups(struct gl_context *ctx)
{
   struct st_context *st = st_context(ctx);
   struct gl_perf_monitor_state *perfmon = &st->ctx->PerfMonitor;
   struct pipe_screen *screen = st->pipe->screen;
   struct gl_perf_monitor_group *groups = NULL;
   struct st_perf_monitor_group *stgroups = NULL;
   int num_counters, num_groups;
   int gid, cid;

   /* Get the number of available queries. */
   num_counters = screen->get_driver_query_info(screen, 0, NULL);

   /* Get the number of available groups. */
   num_groups = screen->get_driver_query_group_info(screen, 0, NULL);
   groups = CALLOC(num_groups, sizeof(*groups));
   if (!groups)
      return;

   stgroups = CALLOC(num_groups, sizeof(*stgroups));
   if (!stgroups)
      goto fail_only_groups;

   for (gid = 0; gid < num_groups; gid++) {
      struct gl_perf_monitor_group *g = &groups[perfmon->NumGroups];
      struct st_perf_monitor_group *stg = &stgroups[perfmon->NumGroups];
      struct pipe_driver_query_group_info group_info;
      struct gl_perf_monitor_counter *counters = NULL;
      struct st_perf_monitor_counter *stcounters = NULL;

      if (!screen->get_driver_query_group_info(screen, gid, &group_info))
         continue;

      g->Name = group_info.name;
      g->MaxActiveCounters = group_info.max_active_queries;

      if (group_info.num_queries)
         counters = CALLOC(group_info.num_queries, sizeof(*counters));
      if (!counters)
         goto fail;
      g->Counters = counters;

      stcounters = CALLOC(group_info.num_queries, sizeof(*stcounters));
      if (!stcounters)
         goto fail;
      stg->counters = stcounters;

      for (cid = 0; cid < num_counters; cid++) {
         struct gl_perf_monitor_counter *c = &counters[g->NumCounters];
         struct st_perf_monitor_counter *stc = &stcounters[g->NumCounters];
         struct pipe_driver_query_info info;

         if (!screen->get_driver_query_info(screen, cid, &info))
            continue;
         if (info.group_id != gid)
            continue;

         c->Name = info.name;
         switch (info.type) {
            case PIPE_DRIVER_QUERY_TYPE_UINT64:
            case PIPE_DRIVER_QUERY_TYPE_BYTES:
            case PIPE_DRIVER_QUERY_TYPE_MICROSECONDS:
            case PIPE_DRIVER_QUERY_TYPE_HZ:
               c->Minimum.u64 = 0;
               c->Maximum.u64 = info.max_value.u64 ? info.max_value.u64 : -1;
               c->Type = GL_UNSIGNED_INT64_AMD;
               break;
            case PIPE_DRIVER_QUERY_TYPE_UINT:
               c->Minimum.u32 = 0;
               c->Maximum.u32 = info.max_value.u32 ? info.max_value.u32 : -1;
               c->Type = GL_UNSIGNED_INT;
               break;
            case PIPE_DRIVER_QUERY_TYPE_FLOAT:
               c->Minimum.f = 0.0;
               c->Maximum.f = info.max_value.f ? info.max_value.f : -1;
               c->Type = GL_FLOAT;
               break;
            case PIPE_DRIVER_QUERY_TYPE_PERCENTAGE:
               c->Minimum.f = 0.0f;
               c->Maximum.f = 100.0f;
               c->Type = GL_PERCENTAGE_AMD;
               break;
            default:
               unreachable("Invalid driver query type!");
         }

         stc->query_type = info.query_type;
         stc->flags = info.flags;
         if (stc->flags & PIPE_DRIVER_QUERY_FLAG_BATCH)
            stg->has_batch = true;

         g->NumCounters++;
      }
      perfmon->NumGroups++;
   }
   perfmon->Groups = groups;
   st->perfmon = stgroups;

   return;

fail:
   for (gid = 0; gid < num_groups; gid++) {
      FREE(stgroups[gid].counters);
      FREE((void *)groups[gid].Counters);
   }
   FREE(stgroups);
fail_only_groups:
   FREE(groups);
}

void
st_destroy_perfmon(struct st_context *st)
{
   struct gl_perf_monitor_state *perfmon = &st->ctx->PerfMonitor;
   int gid;

   for (gid = 0; gid < perfmon->NumGroups; gid++) {
      FREE(st->perfmon[gid].counters);
      FREE((void *)perfmon->Groups[gid].Counters);
   }
   FREE(st->perfmon);
   FREE((void *)perfmon->Groups);
}

void st_init_perfmon_functions(struct dd_function_table *functions)
{
   functions->InitPerfMonitorGroups = st_InitPerfMonitorGroups;
   functions->NewPerfMonitor = st_NewPerfMonitor;
   functions->DeletePerfMonitor = st_DeletePerfMonitor;
   functions->BeginPerfMonitor = st_BeginPerfMonitor;
   functions->EndPerfMonitor = st_EndPerfMonitor;
   functions->ResetPerfMonitor = st_ResetPerfMonitor;
   functions->IsPerfMonitorResultAvailable = st_IsPerfMonitorResultAvailable;
   functions->GetPerfMonitorResult = st_GetPerfMonitorResult;
}
