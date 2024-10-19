/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_UTIL_H_
#define FD5_UTIL_H_

#include "freedreno_util.h"

#include "a5xx.xml.h"

enum a5xx_vtx_fmt fd5_pipe2vtx(enum pipe_format format);
enum a5xx_tex_fmt fd5_pipe2tex(enum pipe_format format);
enum a5xx_color_fmt fd5_pipe2color(enum pipe_format format);
enum a3xx_color_swap fd5_pipe2swap(enum pipe_format format);
enum a5xx_depth_format fd5_pipe2depth(enum pipe_format format);

uint32_t fd5_tex_swiz(enum pipe_format format, unsigned swizzle_r,
                      unsigned swizzle_g, unsigned swizzle_b,
                      unsigned swizzle_a);

#endif /* FD5_UTIL_H_ */
