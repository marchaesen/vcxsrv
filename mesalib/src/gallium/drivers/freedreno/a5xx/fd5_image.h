/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_IMAGE_H_
#define FD5_IMAGE_H_

#include "freedreno_context.h"

struct ir3_shader_variant;
void fd5_emit_images(struct fd_context *ctx, struct fd_ringbuffer *ring,
                     enum pipe_shader_type shader,
                     const struct ir3_shader_variant *v);

#endif /* FD5_IMAGE_H_ */
