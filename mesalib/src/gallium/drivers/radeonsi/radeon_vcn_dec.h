/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
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

#ifndef _RADEON_VCN_DEC_H
#define _RADEON_VCN_DEC_H

#include "radeon_vcn.h"
#include "util/list.h"

#include "ac_vcn_dec.h"

#define NUM_BUFFERS                                         4

struct rvcn_dec_dynamic_dpb_t2 {
   struct list_head list;
   uint8_t index;
   struct rvid_buffer dpb;
};

struct radeon_decoder {
   struct pipe_video_codec base;

   unsigned stream_handle;
   unsigned stream_type;
   unsigned frame_number;
   unsigned db_alignment;
   unsigned dpb_size;
   unsigned last_width;
   unsigned last_height;
   unsigned addr_gfx_mode;

   struct pipe_screen *screen;
   struct radeon_winsys *ws;
   struct radeon_cmdbuf cs;

   void *msg;
   uint32_t *fb;
   uint8_t *it;
   uint8_t *probs;
   void *bs_ptr;
   rvcn_decode_buffer_t *decode_buffer;
   bool vcn_dec_sw_ring;
   struct rvcn_sq_var sq;

   struct rvid_buffer msg_fb_it_probs_buffers[NUM_BUFFERS];
   struct rvid_buffer bs_buffers[NUM_BUFFERS];
   struct rvid_buffer dpb;
   struct rvid_buffer ctx;
   struct rvid_buffer sessionctx;

   unsigned bs_size;
   unsigned cur_buffer;
   void *render_pic_list[32];
   unsigned h264_valid_ref_num[17];
   unsigned h264_valid_poc_num[34];
   unsigned av1_version;
   bool show_frame;
   unsigned ref_idx;
   bool tmz_ctx;
   struct {
      unsigned data0;
      unsigned data1;
      unsigned cmd;
      unsigned cntl;
   } reg;
   struct jpeg_params jpg;
   enum {
      DPB_MAX_RES = 0,
      DPB_DYNAMIC_TIER_1,
      DPB_DYNAMIC_TIER_2
   } dpb_type;

   struct {
      enum {
         CODEC_8_BITS = 0,
         CODEC_10_BITS
      } bts;
      uint8_t index;
      unsigned ref_size;
      uint8_t ref_list[16];
   } ref_codec;

   struct list_head dpb_ref_list;
   struct list_head dpb_unref_list;

   void (*send_cmd)(struct radeon_decoder *dec, struct pipe_video_buffer *target,
                    struct pipe_picture_desc *picture);
   /* Additional contexts for mJPEG */
   struct radeon_cmdbuf *jcs;
   struct radeon_winsys_ctx **jctx;
   unsigned cb_idx;
   unsigned njctx;
};

void send_cmd_dec(struct radeon_decoder *dec, struct pipe_video_buffer *target,
                  struct pipe_picture_desc *picture);

void send_cmd_jpeg(struct radeon_decoder *dec, struct pipe_video_buffer *target,
                   struct pipe_picture_desc *picture);

struct pipe_video_codec *radeon_create_decoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templat);

#endif
