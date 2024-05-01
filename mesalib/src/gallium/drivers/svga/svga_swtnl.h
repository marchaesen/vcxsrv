/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_SWTNL_H
#define SVGA_SWTNL_H

#include "util/compiler.h"

struct svga_context;
struct pipe_context;
struct vbuf_render;


bool svga_init_swtnl( struct svga_context *svga );
void svga_destroy_swtnl( struct svga_context *svga );


enum pipe_error
svga_swtnl_draw_vbo(struct svga_context *svga,
                    const struct pipe_draw_info *info,
                    unsigned drawid_offset,
                    const struct pipe_draw_indirect_info *indirect,
                    const struct pipe_draw_start_count_bias *draw);


#endif
