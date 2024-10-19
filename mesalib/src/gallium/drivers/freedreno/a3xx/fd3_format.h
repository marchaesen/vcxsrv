/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 */

#ifndef FD3_FORMAT_H_
#define FD3_FORMAT_H_

#include "util/format/u_format.h"
#include "freedreno_util.h"

#include "a3xx.xml.h"

enum a3xx_vtx_fmt fd3_pipe2vtx(enum pipe_format format);
enum a3xx_tex_fmt fd3_pipe2tex(enum pipe_format format);
enum a3xx_color_fmt fd3_pipe2color(enum pipe_format format);
enum a3xx_color_fmt fd3_fs_output_format(enum pipe_format format);
enum a3xx_color_swap fd3_pipe2swap(enum pipe_format format);

uint32_t fd3_tex_swiz(enum pipe_format format, unsigned swizzle_r,
                      unsigned swizzle_g, unsigned swizzle_b,
                      unsigned swizzle_a);

#endif /* FD3_FORMAT_H_ */
