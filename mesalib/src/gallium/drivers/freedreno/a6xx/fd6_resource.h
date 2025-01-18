/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_RESOURCE_H_
#define FD6_RESOURCE_H_

#include "freedreno_resource.h"

enum fd6_format_status {
   FORMAT_OK,
   DEMOTE_TO_LINEAR,
   DEMOTE_TO_TILED,
};

enum fd6_format_status fd6_check_valid_format(struct fd_resource *rsc,
                                              enum pipe_format format);
void fd6_validate_format(struct fd_context *ctx, struct fd_resource *rsc,
                         enum pipe_format format) assert_dt;

static inline void
fd6_assert_valid_format(struct fd_resource *rsc, enum pipe_format format)
{
   assert(fd6_check_valid_format(rsc, format) == FORMAT_OK);
}

void fd6_emit_flag_reference(struct fd_ringbuffer *ring,
                             struct fd_resource *rsc, int level, int layer);
template <chip CHIP>
void fd6_resource_screen_init(struct pipe_screen *pscreen);

#endif /* FD6_RESOURCE_H_ */
