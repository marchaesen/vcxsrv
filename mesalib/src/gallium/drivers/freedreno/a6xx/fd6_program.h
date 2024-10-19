/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_PROGRAM_H_
#define FD6_PROGRAM_H_

#include "pipe/p_context.h"
#include "freedreno_context.h"

#include "ir3/ir3_shader.h"
#include "ir3_cache.h"

struct fd6_emit;

struct fd6_program_state {
   struct ir3_program_state base;
   const struct ir3_shader_variant *bs; /* binning pass vs */
   const struct ir3_shader_variant *vs;
   const struct ir3_shader_variant *hs;
   const struct ir3_shader_variant *ds;
   const struct ir3_shader_variant *gs;
   const struct ir3_shader_variant *fs;
   struct fd_ringbuffer *config_stateobj;
   struct fd_ringbuffer *interp_stateobj;
   struct fd_ringbuffer *binning_stateobj;
   struct fd_ringbuffer *streamout_stateobj;
   struct fd_ringbuffer *stateobj;

   const struct ir3_stream_output_info *stream_output;

   /**
    * Whether multiple viewports are used is determined by whether
    * the last shader stage writes viewport id
    */
   uint8_t num_viewports;

   /**
    * The # of shader stages that need driver params.
    */
   uint8_t num_driver_params;

   /**
    * The # of shader stages that need ubo driver params
    */
   uint8_t num_ubo_driver_params;

   /**
    * Output components from frag shader.  It is possible to have
    * a fragment shader that only writes a subset of the bound
    * render targets.
    */
   uint32_t mrt_components;

   /**
    * Rather than calculating user consts state size each draw,
    * calculate it up-front.
    */
   uint32_t user_consts_cmdstream_size;

   /**
    * The FS contribution to LRZ state
    */
   struct fd6_lrz_state lrz_mask;
};

static inline struct fd6_program_state *
fd6_program_state(struct ir3_program_state *state)
{
   return (struct fd6_program_state *)state;
}

static inline const struct ir3_shader_variant *
fd6_last_shader(const struct fd6_program_state *state)
{
   if (state->gs)
      return state->gs;
   else if (state->ds)
      return state->ds;
   else
      return state->vs;
}

template <chip CHIP>
void fd6_emit_shader(struct fd_context *ctx, struct fd_ringbuffer *ring,
                     const struct ir3_shader_variant *so) assert_dt;

struct fd_ringbuffer *fd6_program_interp_state(struct fd6_emit *emit) assert_dt;

template <chip CHIP>
void fd6_prog_init(struct pipe_context *pctx);

#endif /* FD6_PROGRAM_H_ */
