/*
 * Copyright 2022 Kylin Software Co., Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <string.h>
#include <sys/param.h>

#include "vl/vl_decoder.h"
#include "vl/vl_video_buffer.h"
#include "util/u_video.h"
#include "util/u_memory.h"

#include "virgl_screen.h"
#include "virgl_resource.h"
#include "virgl_encode.h"
#include "virgl_video.h"

/*
 * The max size of bs buffer is approximately:
 *   num_of_macroblocks * max_size_of_per_macroblock + size_of_some_headers
 * Now, we only support YUV420 formats, this means that we have a limit of
 * 3200 bits(400 Bytes) per macroblock. To simplify the calculation, we
 * directly use 512 instead of 400.
 */
#define BS_BUF_DEFAULT_SIZE(width, height) \
    ((width) * (height) / (VL_MACROBLOCK_WIDTH * VL_MACROBLOCK_HEIGHT) * 512)

static void switch_buffer(struct virgl_video_codec *vcdc)
{
    vcdc->cur_buffer++;
    vcdc->cur_buffer %= VIRGL_VIDEO_CODEC_BUF_NUM;
}

#define ITEM_SET(dest, src, item)   (dest)->item = (src)->item
#define ITEM_CPY(dest, src, item)   memcpy(&(dest)->item, &(src)->item, sizeof((dest)->item))

static int fill_base_picture_desc(const struct pipe_picture_desc *desc,
                                  struct virgl_base_picture_desc *vbase)
{
    ITEM_SET(vbase, desc, profile);
    ITEM_SET(vbase, desc, entry_point);
    ITEM_SET(vbase, desc, protected_playback);
    ITEM_SET(vbase, desc, key_size);
    memcpy(vbase->decrypt_key, desc->decrypt_key,
           MIN(desc->key_size, sizeof(vbase->decrypt_key)));

    return 0;
}

static int fill_h264_picture_desc(const struct pipe_picture_desc *desc,
                                  union virgl_picture_desc *vdsc)
{
    unsigned i;
    struct virgl_video_buffer *vbuf;

    struct virgl_h264_picture_desc *vh264 = &vdsc->h264;
    struct virgl_h264_pps *vpps = &vh264->pps;
    struct virgl_h264_sps *vsps = &vh264->pps.sps;

    struct pipe_h264_picture_desc *h264 = (struct pipe_h264_picture_desc *)desc;
    struct pipe_h264_pps *pps = h264->pps;
    struct pipe_h264_sps *sps = h264->pps->sps;

    fill_base_picture_desc(desc, &vh264->base);

    ITEM_SET(vsps, sps, level_idc);
    ITEM_SET(vsps, sps, chroma_format_idc);
    ITEM_SET(vsps, sps, separate_colour_plane_flag);
    ITEM_SET(vsps, sps, bit_depth_luma_minus8);
    ITEM_SET(vsps, sps, bit_depth_chroma_minus8);
    ITEM_SET(vsps, sps, seq_scaling_matrix_present_flag);
    ITEM_CPY(vsps, sps, ScalingList4x4);
    ITEM_CPY(vsps, sps, ScalingList8x8);
    ITEM_SET(vsps, sps, log2_max_frame_num_minus4);
    ITEM_SET(vsps, sps, pic_order_cnt_type);
    ITEM_SET(vsps, sps, log2_max_pic_order_cnt_lsb_minus4);
    ITEM_SET(vsps, sps, delta_pic_order_always_zero_flag);
    ITEM_SET(vsps, sps, offset_for_non_ref_pic);
    ITEM_SET(vsps, sps, offset_for_top_to_bottom_field);
    ITEM_CPY(vsps, sps, offset_for_ref_frame);
    ITEM_SET(vsps, sps, num_ref_frames_in_pic_order_cnt_cycle);
    ITEM_SET(vsps, sps, max_num_ref_frames);
    ITEM_SET(vsps, sps, frame_mbs_only_flag);
    ITEM_SET(vsps, sps, mb_adaptive_frame_field_flag);
    ITEM_SET(vsps, sps, direct_8x8_inference_flag);
    ITEM_SET(vsps, sps, MinLumaBiPredSize8x8);

    ITEM_SET(vpps, pps, entropy_coding_mode_flag);
    ITEM_SET(vpps, pps, bottom_field_pic_order_in_frame_present_flag);
    ITEM_SET(vpps, pps, num_slice_groups_minus1);
    ITEM_SET(vpps, pps, slice_group_map_type);
    ITEM_SET(vpps, pps, slice_group_change_rate_minus1);
    ITEM_SET(vpps, pps, num_ref_idx_l0_default_active_minus1);
    ITEM_SET(vpps, pps, num_ref_idx_l1_default_active_minus1);
    ITEM_SET(vpps, pps, weighted_pred_flag);
    ITEM_SET(vpps, pps, weighted_bipred_idc);
    ITEM_SET(vpps, pps, pic_init_qp_minus26);
    ITEM_SET(vpps, pps, pic_init_qs_minus26);
    ITEM_SET(vpps, pps, chroma_qp_index_offset);
    ITEM_SET(vpps, pps, deblocking_filter_control_present_flag);
    ITEM_SET(vpps, pps, constrained_intra_pred_flag);
    ITEM_SET(vpps, pps, redundant_pic_cnt_present_flag);
    ITEM_CPY(vpps, pps, ScalingList4x4);
    ITEM_CPY(vpps, pps, ScalingList8x8);
    ITEM_SET(vpps, pps, transform_8x8_mode_flag);
    ITEM_SET(vpps, pps, second_chroma_qp_index_offset);

    ITEM_SET(vh264, h264, frame_num);
    ITEM_SET(vh264, h264, field_pic_flag);
    ITEM_SET(vh264, h264, bottom_field_flag);
    ITEM_SET(vh264, h264, num_ref_idx_l0_active_minus1);
    ITEM_SET(vh264, h264, num_ref_idx_l1_active_minus1);
    ITEM_SET(vh264, h264, slice_count);
    ITEM_CPY(vh264, h264, field_order_cnt);
    ITEM_SET(vh264, h264, is_reference);
    ITEM_SET(vh264, h264, num_ref_frames);
    ITEM_CPY(vh264, h264, field_order_cnt_list);
    ITEM_CPY(vh264, h264, frame_num_list);

    for (i = 0; i < 16; i++) {
        ITEM_SET(vh264, h264, is_long_term[i]);
        ITEM_SET(vh264, h264, top_is_reference[i]);
        ITEM_SET(vh264, h264, bottom_is_reference[i]);

        vbuf = virgl_video_buffer(h264->ref[i]);
        vh264->buffer_id[i] = vbuf ? vbuf->handle : 0;
    }

    return 0;
}

static int fill_h265_picture_desc(const struct pipe_picture_desc *desc,
                                  union virgl_picture_desc *vdsc)
{
    unsigned i;
    struct virgl_video_buffer *vbuf;

    struct virgl_h265_picture_desc *vh265 = &vdsc->h265;
    struct pipe_h265_picture_desc *h265 = (struct pipe_h265_picture_desc *)desc;

    fill_base_picture_desc(desc, &vh265->base);

    ITEM_SET(&vh265->pps.sps, h265->pps->sps, chroma_format_idc);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, separate_colour_plane_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, pic_width_in_luma_samples);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, pic_height_in_luma_samples);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, bit_depth_luma_minus8);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, bit_depth_chroma_minus8);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_max_pic_order_cnt_lsb_minus4);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, sps_max_dec_pic_buffering_minus1);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_min_luma_coding_block_size_minus3);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_diff_max_min_luma_coding_block_size);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_min_transform_block_size_minus2);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_diff_max_min_transform_block_size);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, max_transform_hierarchy_depth_inter);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, max_transform_hierarchy_depth_intra);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, scaling_list_enabled_flag);
    ITEM_CPY(&vh265->pps.sps, h265->pps->sps, ScalingList4x4);
    ITEM_CPY(&vh265->pps.sps, h265->pps->sps, ScalingList8x8);
    ITEM_CPY(&vh265->pps.sps, h265->pps->sps, ScalingList16x16);
    ITEM_CPY(&vh265->pps.sps, h265->pps->sps, ScalingList32x32);
    ITEM_CPY(&vh265->pps.sps, h265->pps->sps, ScalingListDCCoeff16x16);
    ITEM_CPY(&vh265->pps.sps, h265->pps->sps, ScalingListDCCoeff32x32);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, amp_enabled_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, sample_adaptive_offset_enabled_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, pcm_enabled_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, pcm_sample_bit_depth_luma_minus1);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, pcm_sample_bit_depth_chroma_minus1);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_min_pcm_luma_coding_block_size_minus3);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, log2_diff_max_min_pcm_luma_coding_block_size);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, pcm_loop_filter_disabled_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, num_short_term_ref_pic_sets);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, long_term_ref_pics_present_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, num_long_term_ref_pics_sps);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, sps_temporal_mvp_enabled_flag);
    ITEM_SET(&vh265->pps.sps, h265->pps->sps, strong_intra_smoothing_enabled_flag);

    ITEM_SET(&vh265->pps, h265->pps, dependent_slice_segments_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, output_flag_present_flag);
    ITEM_SET(&vh265->pps, h265->pps, num_extra_slice_header_bits);
    ITEM_SET(&vh265->pps, h265->pps, sign_data_hiding_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, cabac_init_present_flag);
    ITEM_SET(&vh265->pps, h265->pps, num_ref_idx_l0_default_active_minus1);
    ITEM_SET(&vh265->pps, h265->pps, num_ref_idx_l1_default_active_minus1);
    ITEM_SET(&vh265->pps, h265->pps, init_qp_minus26);
    ITEM_SET(&vh265->pps, h265->pps, constrained_intra_pred_flag);
    ITEM_SET(&vh265->pps, h265->pps, transform_skip_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, cu_qp_delta_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, diff_cu_qp_delta_depth);
    ITEM_SET(&vh265->pps, h265->pps, pps_cb_qp_offset);
    ITEM_SET(&vh265->pps, h265->pps, pps_cr_qp_offset);
    ITEM_SET(&vh265->pps, h265->pps, pps_slice_chroma_qp_offsets_present_flag);
    ITEM_SET(&vh265->pps, h265->pps, weighted_pred_flag);
    ITEM_SET(&vh265->pps, h265->pps, weighted_bipred_flag);
    ITEM_SET(&vh265->pps, h265->pps, transquant_bypass_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, tiles_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, entropy_coding_sync_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, num_tile_columns_minus1);
    ITEM_SET(&vh265->pps, h265->pps, num_tile_rows_minus1);
    ITEM_SET(&vh265->pps, h265->pps, uniform_spacing_flag);
    ITEM_CPY(&vh265->pps, h265->pps, column_width_minus1);
    ITEM_CPY(&vh265->pps, h265->pps, row_height_minus1);
    ITEM_SET(&vh265->pps, h265->pps, loop_filter_across_tiles_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, pps_loop_filter_across_slices_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, deblocking_filter_control_present_flag);
    ITEM_SET(&vh265->pps, h265->pps, deblocking_filter_override_enabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, pps_deblocking_filter_disabled_flag);
    ITEM_SET(&vh265->pps, h265->pps, pps_beta_offset_div2);
    ITEM_SET(&vh265->pps, h265->pps, pps_tc_offset_div2);
    ITEM_SET(&vh265->pps, h265->pps, lists_modification_present_flag);
    ITEM_SET(&vh265->pps, h265->pps, log2_parallel_merge_level_minus2);
    ITEM_SET(&vh265->pps, h265->pps, slice_segment_header_extension_present_flag);
    ITEM_SET(&vh265->pps, h265->pps, st_rps_bits);

    ITEM_SET(vh265, h265, IDRPicFlag);
    ITEM_SET(vh265, h265, RAPPicFlag);
    ITEM_SET(vh265, h265, CurrRpsIdx);
    ITEM_SET(vh265, h265, NumPocTotalCurr);
    ITEM_SET(vh265, h265, NumDeltaPocsOfRefRpsIdx);
    ITEM_SET(vh265, h265, NumShortTermPictureSliceHeaderBits);
    ITEM_SET(vh265, h265, NumLongTermPictureSliceHeaderBits);

    ITEM_SET(vh265, h265, CurrPicOrderCntVal);
    for (i = 0; i < 16; i++) {
        vbuf = virgl_video_buffer(h265->ref[i]);
        vh265->ref[i] = vbuf ? vbuf->handle : 0;
    }
    ITEM_CPY(vh265, h265, PicOrderCntVal);
    ITEM_CPY(vh265, h265, IsLongTerm);
    ITEM_SET(vh265, h265, NumPocStCurrBefore);
    ITEM_SET(vh265, h265, NumPocStCurrAfter);
    ITEM_SET(vh265, h265, NumPocLtCurr);
    ITEM_CPY(vh265, h265, RefPicSetStCurrBefore);
    ITEM_CPY(vh265, h265, RefPicSetStCurrAfter);
    ITEM_CPY(vh265, h265, RefPicSetLtCurr);
    ITEM_CPY(vh265, h265, RefPicList);
    ITEM_SET(vh265, h265, UseRefPicList);
    ITEM_SET(vh265, h265, UseStRpsBits);

    return 0;
}

static int fill_mpeg4_picture_desc(const struct pipe_picture_desc *desc,
                                   union virgl_picture_desc *vdsc)
{
    unsigned i;
    struct virgl_video_buffer *vbuf;
    struct virgl_mpeg4_picture_desc *vmpeg4 = &vdsc->mpeg4;
    struct pipe_mpeg4_picture_desc *mpeg4 = (struct pipe_mpeg4_picture_desc *)desc;

    fill_base_picture_desc(desc, &vmpeg4->base);

    ITEM_CPY(vmpeg4, mpeg4, trd);
    ITEM_CPY(vmpeg4, mpeg4, trb);
    ITEM_SET(vmpeg4, mpeg4, vop_time_increment_resolution);
    ITEM_SET(vmpeg4, mpeg4, vop_coding_type);
    ITEM_SET(vmpeg4, mpeg4, vop_fcode_forward);
    ITEM_SET(vmpeg4, mpeg4, vop_fcode_backward);
    ITEM_SET(vmpeg4, mpeg4, resync_marker_disable);
    ITEM_SET(vmpeg4, mpeg4, interlaced);
    ITEM_SET(vmpeg4, mpeg4, quant_type);
    ITEM_SET(vmpeg4, mpeg4, quarter_sample);
    ITEM_SET(vmpeg4, mpeg4, short_video_header);
    ITEM_SET(vmpeg4, mpeg4, rounding_control);
    ITEM_SET(vmpeg4, mpeg4, alternate_vertical_scan_flag);
    ITEM_SET(vmpeg4, mpeg4, top_field_first);
    ITEM_CPY(vmpeg4, mpeg4, intra_matrix);
    ITEM_CPY(vmpeg4, mpeg4, non_intra_matrix);
    for (i = 0; i < 16; i++) {
        vbuf = virgl_video_buffer(mpeg4->ref[i]);
        vmpeg4->ref[i] = vbuf ? vbuf->handle : 0;
    }

    return 0;
}

#undef ITEM_SET
#undef ITEM_CPY

static int fill_picture_desc(const struct pipe_picture_desc *desc,
                             union virgl_picture_desc *vdsc)
{
    switch (u_reduce_video_profile(desc->profile)) {
    case PIPE_VIDEO_FORMAT_MPEG4:
        return fill_mpeg4_picture_desc(desc, vdsc);
    case PIPE_VIDEO_FORMAT_MPEG4_AVC:
        return fill_h264_picture_desc(desc, vdsc);
    case PIPE_VIDEO_FORMAT_HEVC:
        return fill_h265_picture_desc(desc, vdsc);
    default:
        return -1;
    }
}

static void virgl_video_begin_frame(struct pipe_video_codec *codec,
                                    struct pipe_video_buffer *target,
                                    struct pipe_picture_desc *picture)
{
    struct virgl_video_codec *vcdc = virgl_video_codec(codec);
    struct virgl_video_buffer *vbuf = virgl_video_buffer(target);

    virgl_encode_begin_frame(vcdc->vctx, vcdc, vbuf);
}

static void virgl_video_decode_macroblock(struct pipe_video_codec *codec,
                                          struct pipe_video_buffer *target,
                                          struct pipe_picture_desc *picture,
                                          const struct pipe_macroblock *macroblocks,
                                          unsigned num_macroblocks)
{
    (void)codec;
    (void)target;
    (void)picture;
    (void)macroblocks;
    (void)num_macroblocks;
}

static void virgl_video_decode_bitstream(struct pipe_video_codec *codec,
                                         struct pipe_video_buffer *target,
                                         struct pipe_picture_desc *picture,
                                         unsigned num_buffers,
                                         const void * const *buffers,
                                         const unsigned *sizes)
{
    struct virgl_video_codec *vcdc = virgl_video_codec(codec);
    struct virgl_video_buffer *vbuf = virgl_video_buffer(target);
    struct virgl_context *vctx = vcdc->vctx;
    struct virgl_screen *vs = virgl_screen(vctx->base.screen);
    struct virgl_resource *vres;
    union  virgl_picture_desc vdsc;
    struct pipe_transfer *xfer = NULL;
    void *ptr;
    unsigned i, total_size;

    /* transfer bitstream data */
    for (i = 0, total_size = 0; i < num_buffers; i++)
        total_size += sizes[i];

    if (total_size > pipe_buffer_size(vcdc->bs_buffers[vcdc->cur_buffer])) {
        pipe_resource_reference(&vcdc->bs_buffers[vcdc->cur_buffer], NULL);
        vcdc->bs_buffers[vcdc->cur_buffer] = pipe_buffer_create(vctx->base.screen,
                        PIPE_BIND_CUSTOM, PIPE_USAGE_STAGING, total_size);
    }

    vctx->base.flush(&vctx->base, NULL, 0);

    vres = virgl_resource(vcdc->bs_buffers[vcdc->cur_buffer]);
    vs->vws->resource_wait(vs->vws, vres->hw_res);
    ptr = pipe_buffer_map(&vctx->base, vcdc->bs_buffers[vcdc->cur_buffer],
                          PIPE_MAP_WRITE, &xfer);
    if (!ptr)
        return;
    for (i = 0, vcdc->bs_size = 0; i < num_buffers; i++) {
        memcpy(ptr + vcdc->bs_size, buffers[i], sizes[i]);
        vcdc->bs_size += sizes[i];
    }
    pipe_buffer_unmap(&vctx->base, xfer);

    /* transfer picture description */
    fill_picture_desc(picture, &vdsc);
    vres = virgl_resource(vcdc->desc_buffers[vcdc->cur_buffer]);
    vs->vws->resource_wait(vs->vws, vres->hw_res);
    ptr = pipe_buffer_map(&vctx->base, vcdc->desc_buffers[vcdc->cur_buffer],
                          PIPE_MAP_WRITE, &xfer);
    if (!ptr)
        return;
    memcpy(ptr, &vdsc, sizeof(vdsc));
    pipe_buffer_unmap(&vctx->base, xfer);

    virgl_encode_decode_bitstream(vctx, vcdc, vbuf, &vdsc, sizeof(vdsc));
}

static void virgl_video_end_frame(struct pipe_video_codec *codec,
                                  struct pipe_video_buffer *target,
                                  struct pipe_picture_desc *picture)
{
    struct virgl_video_codec *vcdc = virgl_video_codec(codec);
    struct virgl_context *vctx = virgl_context(vcdc->base.context);
    struct virgl_video_buffer *vbuf = virgl_video_buffer(target);

    virgl_encode_end_frame(vctx, vcdc, vbuf);
    virgl_flush_eq(vctx, vctx, NULL);

    switch_buffer(vcdc);
}

static void virgl_video_flush(struct pipe_video_codec *codec)
{
    (void)codec;
}

static void virgl_video_get_feedback(struct pipe_video_codec *codec,
                                     void *feedback,
                                     unsigned *size)
{
    (void)codec;
    (void)feedback;
    (void)size;
}

static void virgl_video_destroy_codec(struct pipe_video_codec *codec)
{
    unsigned i;
    struct virgl_video_codec *vcdc = virgl_video_codec(codec);
    struct virgl_context *vctx = virgl_context(vcdc->base.context);

    for (i = 0; i < VIRGL_VIDEO_CODEC_BUF_NUM; i++) {
        pipe_resource_reference(&vcdc->bs_buffers[i], NULL);
        pipe_resource_reference(&vcdc->desc_buffers[i], NULL);
    }

    virgl_encode_destroy_video_codec(vctx, vcdc);

    free(vcdc);
}


struct pipe_video_codec *
virgl_video_create_codec(struct pipe_context *ctx,
                         const struct pipe_video_codec *templ)
{
    unsigned i;
    struct virgl_video_codec *vcdc;
    struct virgl_context *vctx = virgl_context(ctx);
    unsigned width = templ->width, height = templ->height;

    if (virgl_debug & VIRGL_DEBUG_VIDEO)
        debug_printf("VIDEO: create codec. profile=%d, level=%u, entryp=%d, "
                     "chroma_fmt=%d, size=%ux%u, max_ref=%u, expect=%d\n",
                     templ->profile, templ->level, templ->entrypoint,
                     templ->chroma_format, templ->width, templ->height,
                     templ->max_references, templ->expect_chunked_decode);

    /* encode: not supported now */
    if (templ->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE)
        return NULL;

    /* decode: */
    switch (u_reduce_video_profile(templ->profile)) {
    case PIPE_VIDEO_FORMAT_MPEG4: /* fall through */
    case PIPE_VIDEO_FORMAT_MPEG4_AVC:
        width = align(width, VL_MACROBLOCK_WIDTH);
        height = align(height, VL_MACROBLOCK_HEIGHT);
        break;
    case PIPE_VIDEO_FORMAT_HEVC: /* fall through */
    default:
        break;
    }

    vcdc = CALLOC_STRUCT(virgl_video_codec);
    if (!vcdc)
        return NULL;

    vcdc->base = *templ;
    vcdc->base.width = width;
    vcdc->base.height = height;
    vcdc->base.context = ctx;

    vcdc->base.destroy = virgl_video_destroy_codec;
    vcdc->base.begin_frame = virgl_video_begin_frame;
    vcdc->base.decode_macroblock = virgl_video_decode_macroblock;
    vcdc->base.decode_bitstream = virgl_video_decode_bitstream;
    vcdc->base.end_frame = virgl_video_end_frame;
    vcdc->base.flush = virgl_video_flush;
    vcdc->base.get_feedback = virgl_video_get_feedback;

    vcdc->bs_size = 0;
    vcdc->cur_buffer = 0;
    for (i = 0; i < VIRGL_VIDEO_CODEC_BUF_NUM; i++) {
        vcdc->bs_buffers[i] = pipe_buffer_create(ctx->screen,
                          PIPE_BIND_CUSTOM, PIPE_USAGE_STAGING,
                          BS_BUF_DEFAULT_SIZE(width, height));

        vcdc->desc_buffers[i] = pipe_buffer_create(ctx->screen,
                            PIPE_BIND_CUSTOM, PIPE_USAGE_STAGING,
                            sizeof(union virgl_picture_desc));
    }

    vcdc->handle = virgl_object_assign_handle();
    vcdc->vctx = vctx;

    virgl_encode_create_video_codec(vctx, vcdc);

    return &vcdc->base;
}


static void virgl_video_destroy_buffer(struct pipe_video_buffer *buffer)
{
    struct virgl_video_buffer *vbuf = virgl_video_buffer(buffer);

    virgl_encode_destroy_video_buffer(vbuf->vctx, vbuf);

    vl_video_buffer_destroy(buffer);

    free(vbuf);
}

static void virgl_video_destroy_buffer_associated_data(void *data)
{
    (void)data;
}

struct pipe_video_buffer *
virgl_video_create_buffer(struct pipe_context *ctx,
                          const struct pipe_video_buffer *tmpl)
{
    struct virgl_context *vctx = virgl_context(ctx);
    struct virgl_video_buffer *vbuf;

    vbuf = CALLOC_STRUCT(virgl_video_buffer);
    if (!vbuf)
        return NULL;

    vbuf->buf = vl_video_buffer_create(ctx, tmpl);
    if (!vbuf->buf) {
        free(vbuf);
        return NULL;
    }
    vbuf->buf->destroy = virgl_video_destroy_buffer;
    vl_video_buffer_set_associated_data(vbuf->buf,
            NULL, vbuf, virgl_video_destroy_buffer_associated_data);

    vbuf->num_planes = util_format_get_num_planes(vbuf->buf->buffer_format);
    vbuf->plane_views = vbuf->buf->get_sampler_view_planes(vbuf->buf);
    vbuf->handle = virgl_object_assign_handle();
    vbuf->buffer_format = tmpl->buffer_format;
    vbuf->width = tmpl->width;
    vbuf->height = tmpl->height;
    vbuf->vctx = vctx;

    virgl_encode_create_video_buffer(vctx, vbuf);

    if (virgl_debug & VIRGL_DEBUG_VIDEO) {
        debug_printf("VIDEO: create buffer. fmt=%s, %ux%u, num_planes=%u\n",
                     util_format_name(tmpl->buffer_format),
                     tmpl->width, tmpl->height, vbuf->num_planes);

        for (unsigned i = 0; i < vbuf->num_planes; i++)
            if (vbuf->plane_views[i])
                debug_printf("VIDEO: plane[%d]: fmt=%s, target=%u\n", i,
                             util_format_name(vbuf->plane_views[i]->format),
                             vbuf->plane_views[i]->target);
    }

    return vbuf->buf;
}

