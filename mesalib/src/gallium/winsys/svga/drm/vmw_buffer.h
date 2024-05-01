/*
 * Copyright (c) 2009-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */


#ifndef VMW_BUFFER_H_
#define VMW_BUFFER_H_

#include <assert.h>
#include "util/compiler.h"
#include "pipebuffer/pb_bufmgr.h"
#include "util/u_debug_flush.h"


/* These extra flags are used wherever the pb_usage_flags enum type is used */
#define VMW_BUFFER_USAGE_SHARED    (1 << 14)
#define VMW_BUFFER_USAGE_SYNC      (1 << 15)

struct SVGAGuestPtr;
struct pb_buffer;
struct pb_manager;
struct svga_winsys_buffer;
struct svga_winsys_surface;
struct vmw_winsys_screen;

struct vmw_buffer_desc {
   struct pb_desc pb_desc;
   struct vmw_region *region;
};


#if MESA_DEBUG

struct pb_buffer *
vmw_pb_buffer(struct svga_winsys_buffer *buffer);
struct svga_winsys_buffer *
vmw_svga_winsys_buffer_wrap(struct pb_buffer *buffer);
struct debug_flush_buf *
vmw_debug_flush_buf(struct svga_winsys_buffer *buffer);

#else
static inline struct pb_buffer *
vmw_pb_buffer(struct svga_winsys_buffer *buffer)
{
   assert(buffer);
   return (struct pb_buffer *)buffer;
}


static inline struct svga_winsys_buffer *
vmw_svga_winsys_buffer_wrap(struct pb_buffer *buffer)
{
   return (struct svga_winsys_buffer *)buffer;
}
#endif

void
vmw_svga_winsys_buffer_destroy(struct svga_winsys_screen *sws,
                               struct svga_winsys_buffer *buf);
void *
vmw_svga_winsys_buffer_map(struct svga_winsys_screen *sws,
                           struct svga_winsys_buffer *buf,
                           enum pipe_map_flags flags);

void
vmw_svga_winsys_buffer_unmap(struct svga_winsys_screen *sws,
                             struct svga_winsys_buffer *buf);

struct pb_manager *
vmw_dma_bufmgr_create(struct vmw_winsys_screen *vws);

bool
vmw_dma_bufmgr_region_ptr(struct pb_buffer *buf,
                          struct SVGAGuestPtr *ptr);


#endif /* VMW_BUFFER_H_ */
