/*
 * Copyright (c) 2015-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef VMW_DRM_QUERY_H
#define VMW_DRM_QUERY_H

#include "svga3d_reg.h"



/** Guest-backed query */
struct svga_winsys_gb_query
{
   struct svga_winsys_buffer *buf;
};


struct svga_winsys_gb_query *
vmw_svga_winsys_query_create(struct svga_winsys_screen *sws,
                             uint32 queryResultLen);

void
vmw_svga_winsys_query_destroy(struct svga_winsys_screen *sws,
                              struct svga_winsys_gb_query *query);

int
vmw_svga_winsys_query_init(struct svga_winsys_screen *sws,
                           struct svga_winsys_gb_query *query,
                           unsigned offset,
                           SVGA3dQueryState queryState);

void
vmw_svga_winsys_query_get_result(struct svga_winsys_screen *sws,
                       struct svga_winsys_gb_query *query,
                       unsigned offset,
                       SVGA3dQueryState *queryState,
                       void *result, uint32 resultLen);

enum pipe_error
vmw_swc_query_bind(struct svga_winsys_context *swc, 
                   struct svga_winsys_gb_query *query,
                   unsigned flags);

#endif /* VMW_DRM_QUERY_H */

