/*
 * Copyright © 2019 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_COMPUTE_H_
#define FD6_COMPUTE_H_

#include "pipe/p_context.h"

struct fd6_compute_state {
   void *hwcso;    /* ir3_shader_state */
   struct ir3_shader_variant *v;
   struct fd_ringbuffer *stateobj;
   uint32_t user_consts_cmdstream_size;
};

template <chip CHIP>
void fd6_compute_init(struct pipe_context *pctx);

#endif /* FD6_COMPUTE_H_ */
