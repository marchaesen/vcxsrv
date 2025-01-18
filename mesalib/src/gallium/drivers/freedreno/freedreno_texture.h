/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_TEXTURE_H_
#define FREEDRENO_TEXTURE_H_

#include "pipe/p_context.h"

BEGINC;

void fd_sampler_states_bind(struct pipe_context *pctx,
                            enum pipe_shader_type shader, unsigned start,
                            unsigned nr, void **hwcso);

void fd_set_sampler_views(struct pipe_context *pctx,
                          enum pipe_shader_type shader, unsigned start,
                          unsigned nr, unsigned unbind_num_trailing_slots,
                          bool take_ownership,
                          struct pipe_sampler_view **views);

void fd_texture_init(struct pipe_context *pctx);

struct fd_texture_stateobj;

/* Both a3xx/a4xx share the same layout for the border-color buffer,
 * which contains the pre-swizzled (based on texture format) border
 * color value, with the following layout (per sampler):
 *
 *  offset | description
 *  -------+-------------
 *  0x00:  | fp16[0]   \
 *         | fp16[1]   |___ swizzled fp16 channel values for "small float"
 *         | fp16[2]   |    formats (<= 16 bits per component, !integer)
 *         | fp16[3]   /
 *  0x08:  | padding
 *  0x10:  | int16[0]  \
 *         | int16[1]  |___ swizzled int16 channels for "small integer"
 *         | int16[2]  |    formats (<= 16 bits per component, integer)
 *         | int16[3]  /
 *  0x18:  | padding
 *  0x20:  | fp32[0]   \
 *         | fp32[1]   |___ swizzled fp32 channel values for "large float"
 *         | fp32[2]   |    formats (> 16 bits per component, !integer)
 *         | fp32[3]   /
 *  0x30:  | int32[0]  \
 *         | int32[1]  |___ swizzled int32 channel values for "large int"
 *         | int32[2]  |    formats (> 16 bits per component, integer)
 *         | int32[3]  /
 */
#define BORDERCOLOR_SIZE 0x40
void fd_setup_border_colors(struct fd_texture_stateobj *tex, void *ptr,
                            unsigned offset);

ENDC;

#endif /* FREEDRENO_TEXTURE_H_ */
