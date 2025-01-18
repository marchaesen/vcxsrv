/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_QUERY_SW_H_
#define FREEDRENO_QUERY_SW_H_

#include "freedreno_query.h"

/*
 * SW Queries:
 *
 * In the core, we have some support for basic sw counters
 */

struct fd_sw_query {
   struct fd_query base;
   uint64_t begin_value, end_value;
   uint64_t begin_time, end_time;
};

static inline struct fd_sw_query *
fd_sw_query(struct fd_query *q)
{
   return (struct fd_sw_query *)q;
}

struct fd_query *fd_sw_create_query(struct fd_context *ctx, unsigned query_type,
                                    unsigned index);

#endif /* FREEDRENO_QUERY_SW_H_ */
