/*
 * Copyright (c) 2009-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */


#ifndef VMW_FENCE_H_
#define VMW_FENCE_H_


#include "util/compiler.h"
#include "pipebuffer/pb_buffer_fenced.h"

struct pipe_fence_handle;
struct pb_fence_ops;
struct vmw_winsys_screen;


struct pipe_fence_handle *
vmw_fence_create(struct pb_fence_ops *fence_ops,
		 uint32_t handle, uint32_t seqno, uint32_t mask, int32_t fd);

int
vmw_fence_finish(struct vmw_winsys_screen *vws,
		 struct pipe_fence_handle *fence,
		 uint64_t timeout,
		 unsigned flag);

int
vmw_fence_get_fd(struct pipe_fence_handle *fence);

int
vmw_fence_signalled(struct vmw_winsys_screen *vws,
		    struct pipe_fence_handle *fence,
		    unsigned flag);
void
vmw_fence_reference(struct vmw_winsys_screen *vws,
		    struct pipe_fence_handle **ptr,
		    struct pipe_fence_handle *fence);

struct pb_fence_ops *
vmw_fence_ops_create(struct vmw_winsys_screen *vws); 



#endif /* VMW_FENCE_H_ */
