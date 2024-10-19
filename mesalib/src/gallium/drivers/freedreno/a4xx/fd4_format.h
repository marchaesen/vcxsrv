/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_UTIL_H_
#define FD4_UTIL_H_

#include "freedreno_util.h"

#include "a4xx.xml.h"

enum a4xx_vtx_fmt fd4_pipe2vtx(enum pipe_format format);
enum a4xx_tex_fmt fd4_pipe2tex(enum pipe_format format);
enum a4xx_color_fmt fd4_pipe2color(enum pipe_format format);
enum a3xx_color_swap fd4_pipe2swap(enum pipe_format format);
enum a4xx_depth_format fd4_pipe2depth(enum pipe_format format);

uint32_t fd4_tex_swiz(enum pipe_format format, unsigned swizzle_r,
                      unsigned swizzle_g, unsigned swizzle_b,
                      unsigned swizzle_a);

#endif /* FD4_UTIL_H_ */
