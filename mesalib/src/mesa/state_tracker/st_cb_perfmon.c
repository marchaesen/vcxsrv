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

/**
 * Return a PIPE_QUERY_x type >= PIPE_QUERY_DRIVER_SPECIFIC, or -1 if
 * the driver-specific query doesn't exist.
 */
static int
find_query_type(struct pipe_screen *screen, const char *name)
{
   int num_queries;
   int type = -1;
   int i;

   num_queries = screen->get_driver_query_info(screen, 0, NULL);
   if (!num_queries)
      return type;

   for (i = 0; i < num_queries; i++) {
      struct pipe_driver_query_info info;

      if (!screen->get_driver_query_info(screen, i, &info))
         continue;

      if (!strncmp(info.name, name, strlen(name))) {
         type = info.query_type;
         break;
      }
   }
   return type;
}

/**
 * Return TRUE if the underlying driver expose GPU counters.
 */
static bool
has_gpu_counters(struct pipe_screen *screen)
{
   int num_groups, gid;

   num_groups = screen->get_driver_query_group_info(screen, 0, NULL);
   for (gid = 0; gid < num_groups; gid++) {
      struct pipe_driver_query_group_info group_info;

      if (!screen->get_driver_query_group_info(screen, gid, &group_info))
         continue;

      if (group_info.type == PIPE_DRIVER_QUERY_GROUP_TYPE_GPU)
         return true;
   }
   return false;
}

static bool
init_perf_monitor(struct gl_context *ctx, struct gl_perf_monitor_object *m)
{
   struct st_perf_monitor_object *stm = st_perf_monitor_object(m);
   struct pipe_screen *screen = st_context(ctx)->pipe->screen;
   struct pipe_context *pipe = st_context(ctx)->pipe;
   int gid, cid;

   st_flush_bitmap_cache(st_context(ctx));

   /* Create a query for each active counter. */
   for (gid = 0; gid < ctx->PerfMonitor.NumGroups; gid++) {
      const struct gl_perf_monitor_group *g = &ctx->PerfMonitor.Groups[gid];

      if (m->ActiveGroups[gid] > g->MaxActiveCounters) {
         /* Maximum number of counters reached. Cannot start the session. */
         if (ST_DEBUG & DEBUG_MESA) {
            debug_printf("Maximum number of counters reached. "
                         "Cannot start the session!\n");
         }
         return false;
      }

      for (cid = 0; cid < g->NumCounters; cid++) {
         const struct gl_perf_monitor_counter *c = &g->Counters[cid];
         struct st_perf_counter_object *cntr;
         int query_type;

         if (!BITSET_TEST(m->ActiveCounters[gid], cid))
            continue;

         query_type = find_query_type(screen, c->Name);
         assert(query_type != -1);

         cntr = CALLOC_STRUCT(st_perf_counter_object);
         if (!cntr)
            return false;

         cntr->query    = pipe->create_query(pipe, query_type, 0);
         cntr->id       = cid;
         cntr->group_id = gid;

         list_addtail(&cntr->list, &stm->active_counters);
      }
   }
   return true;
}

static void
reset_perf_monitor(struct st_perf_monitor_object *stm,
                   struct pipe_context *pipe)
{
   struct st_perf_counter_object *cntr, *tmp;

   LIST_FOR_EACH_ENTRY_SAFE(cntr, tmp, &stm->active_counters, list) {
      if (cntr->query)
         pipe->destroy_query(pipe, cntr->query);
      list_del(&cntr->list);
      free(cntr);
   }
}

static struct gl_perf_monitor_object *
st_NewPerfMonitor(struct gl_context *ctx)
{
   struct st_perf_monitor_object *stq = ST_CALLOC_STRUCT(st_perf_monitor_object);
   if (stq) {
      list_inithead(&stq->active_counters);
      return &stq->base;
   }
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
   struct st_perf_counter_object *cntr;

   if (LIST_IS_EMPTY(&stm->active_counters)) {
      /* Create a query for each active counter before starting
       * a new monitoring session. */
      if (!init_perf_monitor(ctx, m))
         goto fail;
   }

   /* Start the query for each active counter. */
   LIST_FOR_EACH_ENTRY(cntr, &stm->active_counters, list) {
      if (!pipe->begin_query(pipe, cntr->query))
          goto fail;
   }
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
   struct st_perf_counter_object *cntr;

   /* Stop the query for each active counter. */
   LIST_FOR_EACH_ENTRY(cntr, &stm->active_counters, list)
      pipe->end_query(pipe, cntr->query);
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
   struct st_perf_counter_object *cntr;

   if (LIST_IS_EMPTY(&stm->active_counters))
      return false;

   /* The result of a monitoring session is only available if the query of
    * each active counter is idle. */
   LIST_FOR_EACH_ENTRY(cntr, &stm->active_counters, list) {
      union pipe_query_result result;
      if (!pipe->get_query_result(pipe, cntr->query, FALSE, &result)) {
         /* The query is busy. */
         return false;
      }
   }
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
   struct st_perf_counter_object *cntr;

   /* Copy data to the supplied array (data).
    *
    * The output data format is: <group ID, counter ID, value> for each
    * active counter. The API allows counters to appear in any order.
    */
   GLsizei offset = 0;

   /* Read query results for each active counter. */
   LIST_FOR_EACH_ENTRY(cntr, &stm->active_counters, list) {
      union pipe_query_result result = { 0 };
      int gid, cid;
      GLenum type;

      cid  = cntr->id;
      gid  = cntr->group_id;
      type = ctx->PerfMonitor.Groups[gid].Counters[cid].Type;

      if (!pipe->get_query_result(pipe, cntr->query, TRUE, &result))
         continue;

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
st_init_perfmon(struct st_context *st)
{
   struct gl_perf_monitor_state *perfmon = &st->ctx->PerfMonitor;
   struct pipe_screen *screen = st->pipe->screen;
   struct gl_perf_monitor_group *groups = NULL;
   int num_counters, num_groups;
   int gid, cid;

   if (!screen->get_driver_query_info || !screen->get_driver_query_group_info)
      return false;

   if (!has_gpu_counters(screen)) {
      /* According to the spec, GL_AMD_performance_monitor must only
       * expose GPU counters. */
      return false;
   }

   /* Get the number of available queries. */
   num_counters = screen->get_driver_query_info(screen, 0, NULL);
   if (!num_counters)
      return false;

   /* Get the number of available groups. */
   num_groups = screen->get_driver_query_group_info(screen, 0, NULL);
   if (num_groups)
      groups = CALLOC(num_groups, sizeof(*groups));
   if (!groups)
      return false;

   for (gid = 0; gid < num_groups; gid++) {
      struct gl_perf_monitor_group *g = &groups[perfmon->NumGroups];
      struct pipe_driver_query_group_info group_info;
      struct gl_perf_monitor_counter *counters = NULL;

      if (!screen->get_driver_query_group_info(screen, gid, &group_info))
         continue;

      if (group_info.type != PIPE_DRIVER_QUERY_GROUP_TYPE_GPU)
         continue;

      g->Name = group_info.name;
      g->MaxActiveCounters = group_info.max_active_queries;
      g->NumCounters = 0;
      g->Counters = NULL;

      if (group_info.num_queries)
         counters = CALLOC(group_info.num_queries, sizeof(*counters));
      if (!counters)
         goto fail;

      for (cid = 0; cid < num_counters; cid++) {
         struct gl_perf_monitor_counter *c = &counters[g->NumCounters];
         struct pipe_driver_query_info info;

         if (!screen->get_driver_query_info(screen, cid, &info))
            continue;
         if (info.group_id != gid)
            continue;

         c->Name = info.name;
         switch (info.type) {
            case PIPE_DRIVER_QUERY_TYPE_UINT64:
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
         g->NumCounters++;
      }
      g->Counters = counters;
      perfmon->NumGroups++;
   }
   perfmon->Groups = groups;

   return true;

fail:
   for (gid = 0; gid < num_groups; gid++)
      FREE((void *)groups[gid].Counters);
   FREE(groups);
   return false;
}

void
st_destroy_perfmon(struct st_context *st)
{
   struct gl_perf_monitor_state *perfmon = &st->ctx->PerfMonitor;
   int gid;

   for (gid = 0; gid < perfmon->NumGroups; gid++)
      FREE((void *)perfmon->Groups[gid].Counters);
   FREE((void *)perfmon->Groups);
}

void st_init_perfmon_functions(struct dd_function_table *functions)
{
   functions->NewPerfMonitor = st_NewPerfMonitor;
   functions->DeletePerfMonitor = st_DeletePerfMonitor;
   functions->BeginPerfMonitor = st_BeginPerfMonitor;
   functions->EndPerfMonitor = st_EndPerfMonitor;
   functions->ResetPerfMonitor = st_ResetPerfMonitor;
   functions->IsPerfMonitorResultAvailable = st_IsPerfMonitorResultAvailable;
   functions->GetPerfMonitorResult = st_GetPerfMonitorResult;
}
