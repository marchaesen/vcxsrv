/*
 * Copyright (c) 2014-2024 Broadcom. All Rights Reserved.
 * The term “Broadcom” refers to Broadcom Inc.
 * and/or its subsidiaries.
 * SPDX-License-Identifier: MIT
 */

#ifndef SVGA_STREAMOUT_H
#define SVGA_STREAMOUT_H

struct svga_shader;

struct svga_stream_output {
   struct pipe_stream_output_info info;
   unsigned pos_out_index;                  // position output index
   unsigned id;
   unsigned streammask;                     // bitmask to specify which streams are enabled
   unsigned buffer_stream;
   struct svga_winsys_buffer *declBuf;
};

struct svga_stream_output *
svga_create_stream_output(struct svga_context *svga,
                          struct svga_shader *shader,
                          const struct pipe_stream_output_info *info);

enum pipe_error
svga_set_stream_output(struct svga_context *svga,
                       struct svga_stream_output *streamout);

void
svga_delete_stream_output(struct svga_context *svga,
                          struct svga_stream_output *streamout);

enum pipe_error
svga_rebind_stream_output_targets(struct svga_context *svga);

void
svga_create_stream_output_queries(struct svga_context *svga);

void
svga_destroy_stream_output_queries(struct svga_context *svga);

void
svga_begin_stream_output_queries(struct svga_context *svga, unsigned mask);

void
svga_end_stream_output_queries(struct svga_context *svga, unsigned mask);

unsigned
svga_get_primcount_from_stream_output(struct svga_context *svga,
                                      unsigned stream);

#endif /* SVGA_STREAMOUT_H */
