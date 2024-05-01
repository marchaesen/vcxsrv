/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_DRAW_H
#define SVGA_DRAW_H

#include "util/compiler.h"

#include "svga_hw_reg.h"

struct svga_hwtnl;
struct svga_winsys_context;
struct svga_screen;
struct svga_context;
struct pipe_resource;
struct u_upload_mgr;

struct svga_hwtnl *svga_hwtnl_create(struct svga_context *svga);

void svga_hwtnl_destroy(struct svga_hwtnl *hwtnl);

void svga_hwtnl_set_flatshade(struct svga_hwtnl *hwtnl,
                              bool flatshade, bool flatshade_first);

void svga_hwtnl_set_fillmode(struct svga_hwtnl *hwtnl, unsigned mode);

void
svga_hwtnl_vertex_decls(struct svga_hwtnl *hwtnl,
                        unsigned count,
                        const SVGA3dVertexDecl * decls,
                        const unsigned *buffer_indexes,
                        SVGA3dElementLayoutId layoutId);

void
svga_hwtnl_vertex_buffers(struct svga_hwtnl *hwtnl,
                          unsigned count, struct pipe_vertex_buffer *buffers);

enum pipe_error
svga_hwtnl_draw_arrays(struct svga_hwtnl *hwtnl,
                       enum mesa_prim prim, unsigned start, unsigned count,
                       unsigned start_instance, unsigned instance_count,
                       uint8_t vertices_per_patch);

enum pipe_error
svga_hwtnl_draw_range_elements(struct svga_hwtnl *hwtnl,
                               const struct pipe_draw_info *info,
                               const struct pipe_draw_start_count_bias *draw,
                               unsigned count);

bool
svga_hwtnl_is_buffer_referred(struct svga_hwtnl *hwtnl,
                              struct pipe_resource *buffer);

enum pipe_error svga_hwtnl_flush(struct svga_hwtnl *hwtnl);

void svga_hwtnl_set_index_bias(struct svga_hwtnl *hwtnl, int index_bias);

#endif /* SVGA_DRAW_H_ */
