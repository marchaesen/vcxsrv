/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef vl_deint_filter_cs_h
#define vl_deint_filter_cs_h

#include "vl_deint_filter.h"

bool
vl_deint_filter_cs_init(struct vl_deint_filter *filter);

void
vl_deint_filter_cs_cleanup(struct vl_deint_filter *filter);

void
vl_deint_filter_cs_render(struct vl_deint_filter *filter,
                          struct pipe_video_buffer *prevprev,
                          struct pipe_video_buffer *prev,
                          struct pipe_video_buffer *cur,
                          struct pipe_video_buffer *next,
                          unsigned field);

#endif /* vl_deint_filter_cs_h */
