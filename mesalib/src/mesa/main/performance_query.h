/*
 * Copyright © 2012,2015 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/**
 * \file performance_query.h
 * Core Mesa support for the INTEL_performance_query extension
 */

#ifndef PERFORMANCE_QUERY_H
#define PERFORMANCE_QUERY_H

#include "glheader.h"

extern void
_mesa_init_performance_queries(struct gl_context *ctx);

extern void
_mesa_free_performance_queries(struct gl_context *ctx);

extern void GLAPIENTRY
_mesa_GetFirstPerfQueryIdINTEL(GLuint *queryId);

extern void GLAPIENTRY
_mesa_GetNextPerfQueryIdINTEL(GLuint queryId, GLuint *nextQueryId);

extern void GLAPIENTRY
_mesa_GetPerfQueryIdByNameINTEL(char *queryName, GLuint *queryId);

extern void GLAPIENTRY
_mesa_GetPerfQueryInfoINTEL(GLuint queryId,
                            GLuint queryNameLength, char *queryName,
                            GLuint *dataSize, GLuint *noCounters,
                            GLuint *noActiveInstances,
                            GLuint *capsMask);

extern void GLAPIENTRY
_mesa_GetPerfCounterInfoINTEL(GLuint queryId, GLuint counterId,
                              GLuint counterNameLength, char *counterName,
                              GLuint counterDescLength, char *counterDesc,
                              GLuint *counterOffset, GLuint *counterDataSize, GLuint *counterTypeEnum,
                              GLuint *counterDataTypeEnum, GLuint64 *rawCounterMaxValue);

extern void GLAPIENTRY
_mesa_CreatePerfQueryINTEL(GLuint queryId, GLuint *queryHandle);

extern void GLAPIENTRY
_mesa_DeletePerfQueryINTEL(GLuint queryHandle);

extern void GLAPIENTRY
_mesa_BeginPerfQueryINTEL(GLuint queryHandle);

extern void GLAPIENTRY
_mesa_EndPerfQueryINTEL(GLuint queryHandle);

extern void GLAPIENTRY
_mesa_GetPerfQueryDataINTEL(GLuint queryHandle, GLuint flags,
                            GLsizei dataSize, void *data, GLuint *bytesWritten);

#endif
