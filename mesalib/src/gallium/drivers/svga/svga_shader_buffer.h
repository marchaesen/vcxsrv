/*
 * Copyright (c) 2022-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_SHADER_BUFFER_H
#define SVGA_SHADER_BUFFER_H

struct svga_shader_buffer {
   struct pipe_shader_buffer desc;
   struct pipe_resource *resource;
   unsigned uav_index;
   struct svga_winsys_surface *handle;
   bool writeAccess;
};

void
svga_init_shader_buffer_functions(struct svga_context *svga);

void
svga_cleanup_shader_buffer_state(struct svga_context *svga);

enum pipe_error
svga_validate_shader_buffer_resources(struct svga_context *svga,
                                      unsigned count,
                                      struct svga_shader_buffer *buffers,
                                      bool rebind);

SVGA3dUAViewId
svga_create_uav_buffer(struct svga_context *svga,
                       const struct pipe_shader_buffer *buf,
                       SVGA3dSurfaceFormat format,
                       SVGA3dUABufferFlags bufFlag);

void
svga_uav_cache_purge_buffers(struct svga_context *svga);

bool
svga_shader_buffer_can_use_srv(struct svga_context *svga,
                               enum pipe_shader_type shader,
                               unsigned index,
                               struct svga_shader_buffer *buffer);

enum pipe_error
svga_shader_buffer_bind_srv(struct svga_context *svga,
                            enum pipe_shader_type shader,
                            unsigned index,
                            struct svga_shader_buffer *buffer);

enum pipe_error
svga_shader_buffer_unbind_srv(struct svga_context *svga,
                              enum pipe_shader_type shader,
                              unsigned index,
                              struct svga_shader_buffer *buffer);

enum pipe_error
svga_emit_rawbuf(struct svga_context *svga,
                 unsigned slot,
                 enum pipe_shader_type shader,
                 unsigned buffer_offset,
                 unsigned buffer_size,
                 void *buffer);

#endif /* SVGA_SHADER_BUFFER_H */
