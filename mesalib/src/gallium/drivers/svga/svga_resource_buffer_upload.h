/*
 * Copyright (c) 2008-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_BUFFER_UPLOAD_H
#define SVGA_BUFFER_UPLOAD_H


void
svga_buffer_upload_flush(struct svga_context *svga,
                         struct svga_buffer *sbuf);

void
svga_buffer_add_range(struct svga_buffer *sbuf,
                      unsigned start,
                      unsigned end);

enum pipe_error
svga_buffer_create_hw_storage(struct svga_screen *ss,
                              struct svga_buffer *sbuf,
                              unsigned bind_flags);

void
svga_buffer_destroy_hw_storage(struct svga_screen *ss,
			       struct svga_buffer *sbuf);

enum pipe_error
svga_buffer_create_host_surface(struct svga_screen *ss,
                                struct svga_buffer *sbuf,
                                unsigned bind_flags);

enum pipe_error
svga_buffer_recreate_host_surface(struct svga_context *svga,
                                  struct svga_buffer *sbuf,
                                  unsigned bind_flags);

struct svga_buffer_surface *
svga_buffer_add_host_surface(struct svga_buffer *sbuf,
                             struct svga_winsys_surface *handle,
                             struct svga_host_surface_cache_key *key,
                             unsigned bind_flags);

void
svga_buffer_bind_host_surface(struct svga_context *svga,
                             struct svga_buffer *sbuf,
                             struct svga_buffer_surface *bufsurf);

enum pipe_error
svga_buffer_validate_host_surface(struct svga_context *svga,
                                  struct svga_buffer *sbuf,
                                  unsigned bind_flags);

void
svga_buffer_destroy_host_surface(struct svga_screen *ss,
                                 struct svga_buffer *sbuf);




#endif /* SVGA_BUFFER_H */
