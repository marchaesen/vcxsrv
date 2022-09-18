/**************************************************************************
 *
 * Copyright 2011 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef RADEON_UVD_H
#define RADEON_UVD_H

#include "winsys/radeon_winsys.h"
#include "vl/vl_video_buffer.h"

#include "ac_uvd_dec.h"

/* driver dependent callback */
typedef struct pb_buffer *(*ruvd_set_dtb)(struct ruvd_msg *msg, struct vl_video_buffer *vb);

/* create an UVD decode */
struct pipe_video_codec *si_common_uvd_create_decoder(struct pipe_context *context,
                                                      const struct pipe_video_codec *templat,
                                                      ruvd_set_dtb set_dtb);

/* fill decoding target field from the luma and chroma surfaces */
void si_uvd_set_dt_surfaces(struct ruvd_msg *msg, struct radeon_surf *luma,
                            struct radeon_surf *chroma, enum ruvd_surface_type type);
#endif
