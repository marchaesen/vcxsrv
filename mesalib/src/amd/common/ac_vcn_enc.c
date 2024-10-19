/**************************************************************************
 *
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdint.h>
#include <stdbool.h>

#include "ac_vcn_enc.h"

#define RENCODE_IB_PARAM_SESSION_INFO                                               0x00000001
#define RENCODE_IB_PARAM_TASK_INFO                                                  0x00000002
#define RENCODE_IB_PARAM_SESSION_INIT                                               0x00000003
#define RENCODE_IB_PARAM_LAYER_CONTROL                                              0x00000004
#define RENCODE_IB_PARAM_LAYER_SELECT                                               0x00000005
#define RENCODE_IB_PARAM_RATE_CONTROL_SESSION_INIT                                  0x00000006
#define RENCODE_IB_PARAM_RATE_CONTROL_LAYER_INIT                                    0x00000007
#define RENCODE_IB_PARAM_RATE_CONTROL_PER_PICTURE                                   0x00000008
#define RENCODE_IB_PARAM_QUALITY_PARAMS                                             0x00000009
#define RENCODE_IB_PARAM_SLICE_HEADER                                               0x0000000a
#define RENCODE_IB_PARAM_ENCODE_PARAMS                                              0x0000000b
#define RENCODE_IB_PARAM_INTRA_REFRESH                                              0x0000000c
#define RENCODE_IB_PARAM_ENCODE_CONTEXT_BUFFER                                      0x0000000d
#define RENCODE_IB_PARAM_VIDEO_BITSTREAM_BUFFER                                     0x0000000e
#define RENCODE_IB_PARAM_FEEDBACK_BUFFER                                            0x00000010
#define RENCODE_IB_PARAM_RATE_CONTROL_PER_PICTURE_EX                                0x0000001d
#define RENCODE_IB_PARAM_DIRECT_OUTPUT_NALU                                         0x00000020
#define RENCODE_IB_PARAM_QP_MAP                                                     0x00000021
#define RENCODE_IB_PARAM_ENCODE_LATENCY                                             0x00000022
#define RENCODE_IB_PARAM_ENCODE_STATISTICS                                          0x00000024

#define RENCODE_HEVC_IB_PARAM_SLICE_CONTROL                                         0x00100001
#define RENCODE_HEVC_IB_PARAM_SPEC_MISC                                             0x00100002
#define RENCODE_HEVC_IB_PARAM_DEBLOCKING_FILTER                                     0x00100003

#define RENCODE_H264_IB_PARAM_SLICE_CONTROL                                         0x00200001
#define RENCODE_H264_IB_PARAM_SPEC_MISC                                             0x00200002
#define RENCODE_H264_IB_PARAM_ENCODE_PARAMS                                         0x00200003
#define RENCODE_H264_IB_PARAM_DEBLOCKING_FILTER                                     0x00200004

#define RENCODE_V2_IB_PARAM_DIRECT_OUTPUT_NALU                                      0x0000000a
#define RENCODE_V2_IB_PARAM_SLICE_HEADER                                            0x0000000b
#define RENCODE_V2_IB_PARAM_INPUT_FORMAT                                            0x0000000c
#define RENCODE_V2_IB_PARAM_OUTPUT_FORMAT                                           0x0000000d
#define RENCODE_V2_IB_PARAM_ENCODE_PARAMS                                           0x0000000f
#define RENCODE_V2_IB_PARAM_INTRA_REFRESH                                           0x00000010
#define RENCODE_V2_IB_PARAM_ENCODE_CONTEXT_BUFFER                                   0x00000011
#define RENCODE_V2_IB_PARAM_VIDEO_BITSTREAM_BUFFER                                  0x00000012
#define RENCODE_V2_IB_PARAM_QP_MAP                                                  0x00000014
#define RENCODE_V2_IB_PARAM_FEEDBACK_BUFFER                                         0x00000015
#define RENCODE_V2_IB_PARAM_ENCODE_LATENCY                                          0x00000018
#define RENCODE_V2_IB_PARAM_ENCODE_STATISTICS                                       0x00000019

#define RENCODE_V4_IB_PARAM_CDF_DEFAULT_TABLE_BUFFER                                0x00000019
#define RENCODE_V4_IB_PARAM_ENCODE_STATISTICS                                       0x0000001a
#define RENCODE_V4_AV1_IB_PARAM_SPEC_MISC                                           0x00300001
#define RENCODE_V4_AV1_IB_PARAM_BITSTREAM_INSTRUCTION                               0x00300002

#define RENCODE_V5_IB_PARAM_METADATA_BUFFER                                         0x0000001c
#define RENCODE_V5_IB_PARAM_ENCODE_CONTEXT_BUFFER_OVERRIDE                          0x0000001d
#define RENCODE_V5_IB_PARAM_HEVC_ENCODE_PARAMS                                      0x00100004
#define RENCODE_V5_AV1_IB_PARAM_TILE_CONFIG                                         0x00300002
#define RENCODE_V5_AV1_IB_PARAM_BITSTREAM_INSTRUCTION                               0x00300003
#define RENCODE_V5_AV1_IB_PARAM_ENCODE_PARAMS                                       0x00300004

void ac_vcn_enc_init_cmds(rvcn_enc_cmd_t *cmd, enum vcn_version version)
{
   cmd->session_info = RENCODE_IB_PARAM_SESSION_INFO;
   cmd->task_info = RENCODE_IB_PARAM_TASK_INFO;
   cmd->session_init = RENCODE_IB_PARAM_SESSION_INIT;
   cmd->layer_control = RENCODE_IB_PARAM_LAYER_CONTROL;
   cmd->layer_select = RENCODE_IB_PARAM_LAYER_SELECT;
   cmd->rc_session_init = RENCODE_IB_PARAM_RATE_CONTROL_SESSION_INIT;
   cmd->rc_layer_init = RENCODE_IB_PARAM_RATE_CONTROL_LAYER_INIT;
   cmd->rc_per_pic = RENCODE_IB_PARAM_RATE_CONTROL_PER_PICTURE;
   cmd->rc_per_pic_ex = RENCODE_IB_PARAM_RATE_CONTROL_PER_PICTURE_EX;
   cmd->quality_params = RENCODE_IB_PARAM_QUALITY_PARAMS;
   cmd->nalu = RENCODE_IB_PARAM_DIRECT_OUTPUT_NALU;
   cmd->slice_header = RENCODE_IB_PARAM_SLICE_HEADER;
   cmd->enc_params = RENCODE_IB_PARAM_ENCODE_PARAMS;
   cmd->intra_refresh = RENCODE_IB_PARAM_INTRA_REFRESH;
   cmd->ctx = RENCODE_IB_PARAM_ENCODE_CONTEXT_BUFFER;
   cmd->bitstream = RENCODE_IB_PARAM_VIDEO_BITSTREAM_BUFFER;
   cmd->feedback = RENCODE_IB_PARAM_FEEDBACK_BUFFER;
   cmd->slice_control_hevc = RENCODE_HEVC_IB_PARAM_SLICE_CONTROL;
   cmd->spec_misc_hevc = RENCODE_HEVC_IB_PARAM_SPEC_MISC;
   cmd->deblocking_filter_hevc = RENCODE_HEVC_IB_PARAM_DEBLOCKING_FILTER;
   cmd->slice_control_h264 = RENCODE_H264_IB_PARAM_SLICE_CONTROL;
   cmd->spec_misc_h264 = RENCODE_H264_IB_PARAM_SPEC_MISC;
   cmd->enc_params_h264 = RENCODE_H264_IB_PARAM_ENCODE_PARAMS;
   cmd->deblocking_filter_h264 = RENCODE_H264_IB_PARAM_DEBLOCKING_FILTER;
   cmd->enc_statistics = RENCODE_IB_PARAM_ENCODE_STATISTICS;
   cmd->enc_qp_map = RENCODE_IB_PARAM_QP_MAP;
   cmd->enc_latency = RENCODE_IB_PARAM_ENCODE_LATENCY;

   if (version >= VCN_2_0_0) {
      cmd->nalu = RENCODE_V2_IB_PARAM_DIRECT_OUTPUT_NALU;
      cmd->slice_header = RENCODE_V2_IB_PARAM_SLICE_HEADER;
      cmd->input_format = RENCODE_V2_IB_PARAM_INPUT_FORMAT;
      cmd->output_format = RENCODE_V2_IB_PARAM_OUTPUT_FORMAT;
      cmd->enc_params = RENCODE_V2_IB_PARAM_ENCODE_PARAMS;
      cmd->intra_refresh = RENCODE_V2_IB_PARAM_INTRA_REFRESH;
      cmd->ctx = RENCODE_V2_IB_PARAM_ENCODE_CONTEXT_BUFFER;
      cmd->bitstream = RENCODE_V2_IB_PARAM_VIDEO_BITSTREAM_BUFFER;
      cmd->feedback = RENCODE_V2_IB_PARAM_FEEDBACK_BUFFER;
      cmd->enc_statistics = RENCODE_V2_IB_PARAM_ENCODE_STATISTICS;
      cmd->enc_qp_map = RENCODE_V2_IB_PARAM_QP_MAP;
      cmd->enc_latency = RENCODE_V2_IB_PARAM_ENCODE_LATENCY;
   }

   if (version >= VCN_4_0_0) {
      cmd->cdf_default_table_av1 = RENCODE_V4_IB_PARAM_CDF_DEFAULT_TABLE_BUFFER;
      cmd->enc_statistics = RENCODE_V4_IB_PARAM_ENCODE_STATISTICS;
      cmd->spec_misc_av1 = RENCODE_V4_AV1_IB_PARAM_SPEC_MISC;
      cmd->bitstream_instruction_av1 = RENCODE_V4_AV1_IB_PARAM_BITSTREAM_INSTRUCTION;
   }

   if (version >= VCN_5_0_0) {
      cmd->metadata = RENCODE_V5_IB_PARAM_METADATA_BUFFER;
      cmd->ctx_override = RENCODE_V5_IB_PARAM_ENCODE_CONTEXT_BUFFER_OVERRIDE;
      cmd->enc_params_hevc = RENCODE_V5_IB_PARAM_HEVC_ENCODE_PARAMS;
      cmd->tile_config_av1 = RENCODE_V5_AV1_IB_PARAM_TILE_CONFIG;
      cmd->bitstream_instruction_av1 = RENCODE_V5_AV1_IB_PARAM_BITSTREAM_INSTRUCTION;
      cmd->enc_params_av1 = RENCODE_V5_AV1_IB_PARAM_ENCODE_PARAMS;
   }
}
