/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FD6_CONST_H
#define FD6_CONST_H

#include "fd6_emit.h"

template <chip CHIP>
struct fd_ringbuffer *fd6_build_tess_consts(struct fd6_emit *emit) assert_dt;
template <chip CHIP>
unsigned fd6_user_consts_cmdstream_size(const struct ir3_shader_variant *v);

template <chip CHIP, fd6_pipeline_type PIPELINE>
struct fd_ringbuffer *fd6_build_user_consts(struct fd6_emit *emit) assert_dt;

template <chip CHIP, fd6_pipeline_type PIPELINE>
struct fd_ringbuffer *
fd6_build_driver_params(struct fd6_emit *emit) assert_dt;

template <chip CHIP>
void fd6_emit_cs_driver_params(struct fd_context *ctx,
                               struct fd_ringbuffer *ring,
                               struct fd6_compute_state *cs,
                               const struct pipe_grid_info *info) assert_dt;
template <chip CHIP>
void fd6_emit_cs_user_consts(struct fd_context *ctx,
                             struct fd_ringbuffer *ring,
                             struct fd6_compute_state *cs) assert_dt;
template <chip CHIP>
void fd6_emit_immediates(const struct ir3_shader_variant *v,
                         struct fd_ringbuffer *ring) assert_dt;
template <chip CHIP>
void fd6_emit_link_map(struct fd_context *ctx,
                       const struct ir3_shader_variant *producer,
                       const struct ir3_shader_variant *consumer,
                       struct fd_ringbuffer *ring) assert_dt;

#endif /* FD6_CONST_H */
