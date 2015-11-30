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

#ifndef ST_CB_PERFMON_H
#define ST_CB_PERFMON_H

#include "util/list.h"

struct st_perf_counter_object
{
   struct pipe_query *query;
   int id;
   int group_id;
   unsigned batch_index;
};

/**
 * Subclass of gl_perf_monitor_object
 */
struct st_perf_monitor_object
{
   struct gl_perf_monitor_object base;
   unsigned num_active_counters;
   struct st_perf_counter_object *active_counters;

   struct pipe_query *batch_query;
   union pipe_query_result *batch_result;
};

/**
 * Extra data per counter, supplementing gl_perf_monitor_counter with
 * driver-specific information.
 */
struct st_perf_monitor_counter
{
   unsigned query_type;
   unsigned flags;
};

struct st_perf_monitor_group
{
   struct st_perf_monitor_counter *counters;
   bool has_batch;
};

/**
 * Cast wrapper
 */
static inline struct st_perf_monitor_object *
st_perf_monitor_object(struct gl_perf_monitor_object *q)
{
   return (struct st_perf_monitor_object *)q;
}

bool
st_have_perfmon(struct st_context *st);

void
st_destroy_perfmon(struct st_context *st);

extern void
st_init_perfmon_functions(struct dd_function_table *functions);

#endif
