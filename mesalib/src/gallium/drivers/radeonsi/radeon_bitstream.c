/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "radeon_bitstream.h"

static const uint32_t index_to_shifts[4] = {24, 16, 8, 0};

static void radeon_bs_output_one_byte(struct radeon_bitstream *bs, uint8_t byte)
{
   if (bs->buf) {
      *(bs->buf++) = byte;
      return;
   }

   if (bs->byte_index == 0)
      bs->cs->current.buf[bs->cs->current.cdw] = 0;
   bs->cs->current.buf[bs->cs->current.cdw] |=
      ((uint32_t)(byte) << index_to_shifts[bs->byte_index]);
   bs->byte_index++;

   if (bs->byte_index >= 4) {
      bs->byte_index = 0;
      bs->cs->current.cdw++;
   }
}

static void radeon_bs_emulation_prevention(struct radeon_bitstream *bs, uint8_t byte)
{
   if (bs->emulation_prevention) {
      if ((bs->num_zeros >= 2) && ((byte == 0x00) || (byte == 0x01) ||
         (byte == 0x02) || (byte == 0x03))) {
         radeon_bs_output_one_byte(bs, 0x03);
         bs->bits_output += 8;
         bs->num_zeros = 0;
      }
      bs->num_zeros = (byte == 0 ? (bs->num_zeros + 1) : 0);
   }
}

void radeon_bs_reset(struct radeon_bitstream *bs, uint8_t *out, struct radeon_cmdbuf *cs)
{
   memset(bs, 0, sizeof(*bs));
   bs->buf = out;
   bs->cs = cs;
}

void radeon_bs_set_emulation_prevention(struct radeon_bitstream *bs, bool set)
{
   if (set != bs->emulation_prevention) {
      bs->emulation_prevention = set;
      bs->num_zeros = 0;
   }
}

void radeon_bs_byte_align(struct radeon_bitstream *bs)
{
   uint32_t num_padding_zeros = (32 - bs->bits_in_shifter) % 8;

   if (num_padding_zeros > 0)
      radeon_bs_code_fixed_bits(bs, 0, num_padding_zeros);
}

void radeon_bs_flush_headers(struct radeon_bitstream *bs)
{
   if (bs->bits_in_shifter != 0) {
      uint8_t output_byte = bs->shifter >> 24;
      radeon_bs_emulation_prevention(bs, output_byte);
      radeon_bs_output_one_byte(bs, output_byte);
      bs->bits_output += bs->bits_in_shifter;
      bs->shifter = 0;
      bs->bits_in_shifter = 0;
      bs->num_zeros = 0;
   }

   if (bs->byte_index > 0) {
      bs->cs->current.cdw++;
      bs->byte_index = 0;
   }
}

void radeon_bs_code_fixed_bits(struct radeon_bitstream *bs, uint32_t value, uint32_t num_bits)
{
   uint32_t bits_to_pack = 0;
   bs->bits_size += num_bits;

   while (num_bits > 0) {
      uint32_t value_to_pack = value & (0xffffffff >> (32 - num_bits));
      bits_to_pack =
         num_bits > (32 - bs->bits_in_shifter) ? (32 - bs->bits_in_shifter) : num_bits;

      if (bits_to_pack < num_bits)
         value_to_pack = value_to_pack >> (num_bits - bits_to_pack);

      bs->shifter |= value_to_pack << (32 - bs->bits_in_shifter - bits_to_pack);
      num_bits -= bits_to_pack;
      bs->bits_in_shifter += bits_to_pack;

      while (bs->bits_in_shifter >= 8) {
         uint8_t output_byte = bs->shifter >> 24;
         bs->shifter <<= 8;
         radeon_bs_emulation_prevention(bs, output_byte);
         radeon_bs_output_one_byte(bs, output_byte);
         bs->bits_in_shifter -= 8;
         bs->bits_output += 8;
      }
   }
}

void radeon_bs_code_ue(struct radeon_bitstream *bs, uint32_t value)
{
   uint32_t x = 0;
   uint32_t ue_code = value + 1;
   value += 1;

   while (value) {
      value = value >> 1;
      x += 1;
   }

   if (x > 1)
     radeon_bs_code_fixed_bits(bs, 0, x - 1);
   radeon_bs_code_fixed_bits(bs, ue_code, x);
}

void radeon_bs_code_se(struct radeon_bitstream *bs, int32_t value)
{
   uint32_t v = 0;

   if (value != 0)
      v = (value < 0 ? ((uint32_t)(0 - value) << 1) : (((uint32_t)(value) << 1) - 1));

   radeon_bs_code_ue(bs, v);
}

void radeon_bs_code_uvlc(struct radeon_bitstream *bs, uint32_t value)
{
   uint32_t num_bits = 0;
   uint64_t value_plus1 = (uint64_t)value + 1;
   uint32_t num_leading_zeros = 0;

   while ((uint64_t)1 << num_bits <= value_plus1)
      num_bits++;

   num_leading_zeros = num_bits - 1;
   radeon_bs_code_fixed_bits(bs, 0, num_leading_zeros);
   radeon_bs_code_fixed_bits(bs, 1, 1);
   radeon_bs_code_fixed_bits(bs, (uint32_t)value_plus1, num_leading_zeros);
}

void radeon_bs_code_ns(struct radeon_bitstream *bs, uint32_t value, uint32_t max)
{
   uint32_t w = 0;
   uint32_t m;
   uint32_t max_num = max;

   while ( max_num ) {
      max_num >>= 1;
      w++;
   }

   m = (1 << w) - max;

   assert(w > 1);

   if (value < m) {
      radeon_bs_code_fixed_bits(bs, value, (w - 1));
   } else {
      uint32_t diff = value - m;
      uint32_t out = (((diff >> 1) + m) << 1) | (diff & 0x1);
      radeon_bs_code_fixed_bits(bs, out, w);
   }
}

void radeon_bs_h264_hrd_parameters(struct radeon_bitstream *bs,
                                   struct pipe_h264_enc_hrd_params *hrd)
{
   radeon_bs_code_ue(bs, hrd->cpb_cnt_minus1);
   radeon_bs_code_fixed_bits(bs, hrd->bit_rate_scale, 4);
   radeon_bs_code_fixed_bits(bs, hrd->cpb_size_scale, 4);
   for (uint32_t i = 0; i <= hrd->cpb_cnt_minus1; i++) {
      radeon_bs_code_ue(bs, hrd->bit_rate_value_minus1[i]);
      radeon_bs_code_ue(bs, hrd->cpb_size_value_minus1[i]);
      radeon_bs_code_fixed_bits(bs, hrd->cbr_flag[i], 1);
   }
   radeon_bs_code_fixed_bits(bs, hrd->initial_cpb_removal_delay_length_minus1, 5);
   radeon_bs_code_fixed_bits(bs, hrd->cpb_removal_delay_length_minus1, 5);
   radeon_bs_code_fixed_bits(bs, hrd->dpb_output_delay_length_minus1, 5);
   radeon_bs_code_fixed_bits(bs, hrd->time_offset_length, 5);
}

static void radeon_bs_hevc_profile_tier(struct radeon_bitstream *bs,
                                        struct pipe_h265_profile_tier *pt)
{
   radeon_bs_code_fixed_bits(bs, pt->general_profile_space, 2);
   radeon_bs_code_fixed_bits(bs, pt->general_tier_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_profile_idc, 5);
   radeon_bs_code_fixed_bits(bs, pt->general_profile_compatibility_flag, 32);
   radeon_bs_code_fixed_bits(bs, pt->general_progressive_source_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_interlaced_source_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_non_packed_constraint_flag, 1);
   radeon_bs_code_fixed_bits(bs, pt->general_frame_only_constraint_flag, 1);
   /* general_reserved_zero_44bits */
   radeon_bs_code_fixed_bits(bs, 0x0, 16);
   radeon_bs_code_fixed_bits(bs, 0x0, 16);
   radeon_bs_code_fixed_bits(bs, 0x0, 12);
}

void radeon_bs_hevc_profile_tier_level(struct radeon_bitstream *bs,
                                       uint32_t max_num_sub_layers_minus1,
                                       struct pipe_h265_profile_tier_level *ptl)
{
   uint32_t i;

   radeon_bs_hevc_profile_tier(bs, &ptl->profile_tier);
   radeon_bs_code_fixed_bits(bs, ptl->general_level_idc, 8);

   for (i = 0; i < max_num_sub_layers_minus1; ++i) {
      radeon_bs_code_fixed_bits(bs, ptl->sub_layer_profile_present_flag[i], 1);
      radeon_bs_code_fixed_bits(bs, ptl->sub_layer_level_present_flag[i], 1);
   }

   if (max_num_sub_layers_minus1 > 0) {
      for (i = max_num_sub_layers_minus1; i < 8; ++i)
         radeon_bs_code_fixed_bits(bs, 0x0, 2); /* reserved_zero_2bits */
   }

   for (i = 0; i < max_num_sub_layers_minus1; ++i) {
      if (ptl->sub_layer_profile_present_flag[i])
         radeon_bs_hevc_profile_tier(bs, &ptl->sub_layer_profile_tier[i]);

      if (ptl->sub_layer_level_present_flag[i])
         radeon_bs_code_fixed_bits(bs, ptl->sub_layer_level_idc[i], 8);
   }
}

static void radeon_bs_hevc_sub_layer_hrd_parameters(struct radeon_bitstream *bs,
                                                    uint32_t cpb_cnt,
                                                    uint32_t sub_pic_hrd_params_present_flag,
                                                    struct pipe_h265_enc_sublayer_hrd_params *hrd)
{
   for (uint32_t i = 0; i < cpb_cnt; i++) {
      radeon_bs_code_ue(bs, hrd->bit_rate_value_minus1[i]);
      radeon_bs_code_ue(bs, hrd->cpb_size_value_minus1[i]);
      if (sub_pic_hrd_params_present_flag) {
         radeon_bs_code_ue(bs, hrd->cpb_size_du_value_minus1[i]);
         radeon_bs_code_ue(bs, hrd->bit_rate_du_value_minus1[i]);
      }
      radeon_bs_code_fixed_bits(bs, hrd->cbr_flag[i], 1);
   }
}

void radeon_bs_hevc_hrd_parameters(struct radeon_bitstream *bs,
                                   uint32_t common_inf_present_flag,
                                   uint32_t max_sub_layers_minus1,
                                   struct pipe_h265_enc_hrd_params *hrd)
{
   if (common_inf_present_flag) {
      radeon_bs_code_fixed_bits(bs, hrd->nal_hrd_parameters_present_flag, 1);
      radeon_bs_code_fixed_bits(bs, hrd->vcl_hrd_parameters_present_flag, 1);
      if (hrd->nal_hrd_parameters_present_flag || hrd->vcl_hrd_parameters_present_flag) {
         radeon_bs_code_fixed_bits(bs, hrd->sub_pic_hrd_params_present_flag, 1);
         if (hrd->sub_pic_hrd_params_present_flag) {
            radeon_bs_code_fixed_bits(bs, hrd->tick_divisor_minus2, 8);
            radeon_bs_code_fixed_bits(bs, hrd->du_cpb_removal_delay_increment_length_minus1, 5);
            radeon_bs_code_fixed_bits(bs, hrd->sub_pic_hrd_params_present_flag, 1);
            radeon_bs_code_fixed_bits(bs, hrd->dpb_output_delay_du_length_minus1, 5);
         }
         radeon_bs_code_fixed_bits(bs, hrd->bit_rate_scale, 4);
         radeon_bs_code_fixed_bits(bs, hrd->cpb_rate_scale, 4);
         if (hrd->sub_pic_hrd_params_present_flag)
            radeon_bs_code_fixed_bits(bs, hrd->cpb_size_du_scale, 4);
         radeon_bs_code_fixed_bits(bs, hrd->initial_cpb_removal_delay_length_minus1, 5);
         radeon_bs_code_fixed_bits(bs, hrd->au_cpb_removal_delay_length_minus1, 5);
         radeon_bs_code_fixed_bits(bs, hrd->dpb_output_delay_length_minus1, 5);
      }
   }

   for (uint32_t i = 0; i <= max_sub_layers_minus1; i++) {
      radeon_bs_code_fixed_bits(bs, hrd->fixed_pic_rate_general_flag[i], 1);
      if (!hrd->fixed_pic_rate_general_flag[i])
         radeon_bs_code_fixed_bits(bs, hrd->fixed_pic_rate_within_cvs_flag[i], 1);
      if (hrd->fixed_pic_rate_within_cvs_flag[i])
         radeon_bs_code_ue(bs, hrd->elemental_duration_in_tc_minus1[i]);
      else
         radeon_bs_code_fixed_bits(bs, hrd->low_delay_hrd_flag[i], 1);
      if (!hrd->low_delay_hrd_flag[i])
         radeon_bs_code_ue(bs, hrd->cpb_cnt_minus1[i]);
      if (hrd->nal_hrd_parameters_present_flag) {
         radeon_bs_hevc_sub_layer_hrd_parameters(bs,
                                                 hrd->cpb_cnt_minus1[i] + 1,
                                                 hrd->sub_pic_hrd_params_present_flag,
                                                 &hrd->nal_hrd_parameters[i]);
      }
      if (hrd->vcl_hrd_parameters_present_flag) {
         radeon_bs_hevc_sub_layer_hrd_parameters(bs,
                                                 hrd->cpb_cnt_minus1[i] + 1,
                                                 hrd->sub_pic_hrd_params_present_flag,
                                                 &hrd->vlc_hrd_parameters[i]);
      }
   }
}

/* returns NumPicTotalCurr */
uint32_t radeon_bs_hevc_st_ref_pic_set(struct radeon_bitstream *bs,
                                       uint32_t index,
                                       uint32_t num_short_term_ref_pic_sets,
                                       struct pipe_h265_st_ref_pic_set *st_rps)
{
   struct pipe_h265_st_ref_pic_set *ref_rps = NULL;
   struct pipe_h265_st_ref_pic_set *rps = &st_rps[index];
   uint32_t i, num_pic_total_curr = 0;

   if (index)
      radeon_bs_code_fixed_bits(bs, rps->inter_ref_pic_set_prediction_flag, 1);

   if (rps->inter_ref_pic_set_prediction_flag) {
      if (index == num_short_term_ref_pic_sets)
         radeon_bs_code_ue(bs, rps->delta_idx_minus1);
      radeon_bs_code_fixed_bits(bs, rps->delta_rps_sign, 1);
      radeon_bs_code_ue(bs, rps->abs_delta_rps_minus1);
      ref_rps = st_rps + index +
         (1 - 2 * rps->delta_rps_sign) * (st_rps->delta_idx_minus1 + 1);
      for (i = 0; i <= (ref_rps->num_negative_pics + ref_rps->num_positive_pics); i++) {
         radeon_bs_code_fixed_bits(bs, rps->used_by_curr_pic_flag[i], 1);
         if (!rps->used_by_curr_pic_flag[i])
            radeon_bs_code_fixed_bits(bs, rps->use_delta_flag[i], 1);
      }
   } else {
      radeon_bs_code_ue(bs, rps->num_negative_pics);
      radeon_bs_code_ue(bs, rps->num_positive_pics);
      for (i = 0; i < rps->num_negative_pics; i++) {
         radeon_bs_code_ue(bs, rps->delta_poc_s0_minus1[i]);
         radeon_bs_code_fixed_bits(bs, rps->used_by_curr_pic_s0_flag[i], 1);
         if (rps->used_by_curr_pic_s0_flag[i])
            num_pic_total_curr++;
      }
      for (i = 0; i < st_rps->num_positive_pics; i++) {
         radeon_bs_code_ue(bs, rps->delta_poc_s1_minus1[i]);
         radeon_bs_code_fixed_bits(bs, rps->used_by_curr_pic_s1_flag[i], 1);
         if (rps->used_by_curr_pic_s1_flag[i])
            num_pic_total_curr++;
      }
   }

   return num_pic_total_curr;
}
