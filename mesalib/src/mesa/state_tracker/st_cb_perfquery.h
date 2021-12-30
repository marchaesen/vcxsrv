/*
 * Copyright © 2019 Intel Corporation
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

#ifndef ST_CB_PERFQUERY_H
#define ST_CB_PERFQUERY_H

bool
st_have_perfquery(struct st_context *st);

unsigned st_InitPerfQueryInfo(struct gl_context *ctx);

void st_GetPerfQueryInfo(struct gl_context *ctx,
                         unsigned query_index,
                         const char **name,
                         GLuint *data_size,
                         GLuint *n_counters,
                         GLuint *n_active);

void st_GetPerfCounterInfo(struct gl_context *ctx,
                           unsigned query_index,
                           unsigned counter_index,
                           const char **name,
                           const char **desc,
                           GLuint *offset,
                           GLuint *data_size,
                           GLuint *type_enum,
                           GLuint *data_type_enum,
                           GLuint64 *raw_max);

void st_DeletePerfQuery(struct gl_context *ctx, struct gl_perf_query_object *o);
bool st_BeginPerfQuery(struct gl_context *ctx, struct gl_perf_query_object *o);
void st_EndPerfQuery(struct gl_context *ctx, struct gl_perf_query_object *o);
void st_WaitPerfQuery(struct gl_context *ctx, struct gl_perf_query_object *o);
bool st_IsPerfQueryReady(struct gl_context *ctx, struct gl_perf_query_object *o);
bool st_GetPerfQueryData(struct gl_context *ctx,
                         struct gl_perf_query_object *o,
                         GLsizei data_size,
                         GLuint *data,
                         GLuint *bytes_written);

struct gl_perf_query_object *
st_NewPerfQueryObject(struct gl_context *ctx, unsigned query_index);
#endif
