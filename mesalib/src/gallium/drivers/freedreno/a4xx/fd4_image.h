/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_IMAGE_H_
#define FD4_IMAGE_H_

#include "freedreno_context.h"

struct ir3_shader_variant;

void fd4_emit_images(struct fd_context *ctx, struct fd_ringbuffer *ring,
      enum pipe_shader_type shader, const struct ir3_shader_variant *v);

#endif /* FD4_IMAGE_H_ */
