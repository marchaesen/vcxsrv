/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir_meta.h"
#include "ac_nir_helpers.h"
#include "ac_surface.h"
#include "nir_format_convert.h"
#include "compiler/aco_interface.h"
#include "util/format_srgb.h"
#include "util/u_pack_color.h"

static nir_def *
deref_ssa(nir_builder *b, nir_variable *var)
{
   return &nir_build_deref_var(b, var)->def;
}

/* unpack_2x16_signed(src, x, y): x = (int32_t)((uint16_t)src); y = src >> 16; */
static void
unpack_2x16_signed(nir_builder *b, unsigned bit_size, nir_def *src, nir_def **x, nir_def **y)
{
   assert(bit_size == 32 || bit_size == 16);
   *x = nir_unpack_32_2x16_split_x(b, src);
   *y = nir_unpack_32_2x16_split_y(b, src);

   if (bit_size == 32) {
      *x = nir_i2i32(b, *x);
      *y = nir_i2i32(b, *y);
   }
}

static nir_def *
convert_linear_to_srgb(nir_builder *b, nir_def *input)
{
   /* There are small precision differences compared to CB, so the gfx blit will return slightly
    * different results.
    */
   for (unsigned i = 0; i < MIN2(3, input->num_components); i++) {
      input = nir_vector_insert_imm(b, input,
                                    nir_format_linear_to_srgb(b, nir_channel(b, input, i)), i);
   }

   return input;
}

static nir_def *
apply_blit_output_modifiers(nir_builder *b, nir_def *color,
                            const union ac_cs_blit_key *key)
{
   unsigned bit_size = color->bit_size;
   nir_def *zero = nir_imm_intN_t(b, 0, bit_size);

   if (key->sint_to_uint)
      color = nir_imax(b, color, zero);

   if (key->uint_to_sint) {
      color = nir_umin(b, color,
                       nir_imm_intN_t(b, bit_size == 16 ? INT16_MAX : INT32_MAX,
                                      bit_size));
   }

   if (key->dst_is_srgb)
      color = convert_linear_to_srgb(b, color);

   nir_def *one = key->use_integer_one ? nir_imm_intN_t(b, 1, bit_size) :
                                             nir_imm_floatN_t(b, 1, bit_size);

   if (key->is_clear) {
      if (key->last_dst_channel < 3)
         color = nir_trim_vector(b, color, key->last_dst_channel + 1);
   } else {
      assert(key->last_src_channel <= key->last_dst_channel);
      assert(color->num_components == key->last_src_channel + 1);

      /* Set channels not present in src to 0 or 1. */
      if (key->last_src_channel < key->last_dst_channel) {
         color = nir_pad_vector(b, color, key->last_dst_channel + 1);

         for (unsigned chan = key->last_src_channel + 1; chan <= key->last_dst_channel; chan++)
            color = nir_vector_insert_imm(b, color, chan == 3 ? one : zero, chan);
      }

      /* Discard channels not present in dst. The hardware fills unstored channels with 0. */
      if (key->last_dst_channel < key->last_src_channel)
         color = nir_trim_vector(b, color, key->last_dst_channel + 1);
   }

   /* Discard channels not present in dst. The hardware fills unstored channels with 0. */
   if (key->last_dst_channel < 3)
      color = nir_trim_vector(b, color, key->last_dst_channel + 1);

   return color;
}

/* The compute blit shader.
 *
 * Implementation details:
 * - Out-of-bounds dst coordinates are not clamped at all. The hw drops
 *   out-of-bounds stores for us.
 * - Out-of-bounds src coordinates are clamped by emulating CLAMP_TO_EDGE using
 *   the image_size NIR intrinsic.
 * - X/Y flipping just does this in the shader: -threadIDs - 1, assuming the starting coordinates
 *   are 1 pixel after the bottom-right corner, e.g. x + width, matching the gallium behavior.
 * - This list doesn't do it justice.
 */
nir_shader *
ac_create_blit_cs(const struct ac_cs_blit_options *options, const union ac_cs_blit_key *key)
{
   if (options->print_key) {
      fprintf(stderr, "Internal shader: compute_blit\n");
      fprintf(stderr, "   key.use_aco = %u\n", key->use_aco);
      fprintf(stderr, "   key.wg_dim = %u\n", key->wg_dim);
      fprintf(stderr, "   key.has_start_xyz = %u\n", key->has_start_xyz);
      fprintf(stderr, "   key.log_lane_width = %u\n", key->log_lane_width);
      fprintf(stderr, "   key.log_lane_height = %u\n", key->log_lane_height);
      fprintf(stderr, "   key.log_lane_depth = %u\n", key->log_lane_depth);
      fprintf(stderr, "   key.is_clear = %u\n", key->is_clear);
      fprintf(stderr, "   key.src_is_1d = %u\n", key->src_is_1d);
      fprintf(stderr, "   key.dst_is_1d = %u\n", key->dst_is_1d);
      fprintf(stderr, "   key.src_is_msaa = %u\n", key->src_is_msaa);
      fprintf(stderr, "   key.dst_is_msaa = %u\n", key->dst_is_msaa);
      fprintf(stderr, "   key.src_has_z = %u\n", key->src_has_z);
      fprintf(stderr, "   key.dst_has_z = %u\n", key->dst_has_z);
      fprintf(stderr, "   key.a16 = %u\n", key->a16);
      fprintf(stderr, "   key.d16 = %u\n", key->d16);
      fprintf(stderr, "   key.log_samples = %u\n", key->log_samples);
      fprintf(stderr, "   key.sample0_only = %u\n", key->sample0_only);
      fprintf(stderr, "   key.x_clamp_to_edge = %u\n", key->x_clamp_to_edge);
      fprintf(stderr, "   key.y_clamp_to_edge = %u\n", key->y_clamp_to_edge);
      fprintf(stderr, "   key.flip_x = %u\n", key->flip_x);
      fprintf(stderr, "   key.flip_y = %u\n", key->flip_y);
      fprintf(stderr, "   key.sint_to_uint = %u\n", key->sint_to_uint);
      fprintf(stderr, "   key.uint_to_sint = %u\n", key->uint_to_sint);
      fprintf(stderr, "   key.dst_is_srgb = %u\n", key->dst_is_srgb);
      fprintf(stderr, "   key.use_integer_one = %u\n", key->use_integer_one);
      fprintf(stderr, "   key.last_src_channel = %u\n", key->last_src_channel);
      fprintf(stderr, "   key.last_dst_channel = %u\n", key->last_dst_channel);
      fprintf(stderr, "\n");
   }

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options->nir_options,
                                                  "blit_non_scaled_cs");
   b.shader->info.use_aco_amd = options->use_aco ||
                                (key->use_aco && aco_is_gpu_supported(options->info));
   b.shader->info.num_images = key->is_clear ? 1 : 2;
   unsigned image_dst_index = b.shader->info.num_images - 1;
   if (!key->is_clear && key->src_is_msaa)
      BITSET_SET(b.shader->info.msaa_images, 0);
   if (key->dst_is_msaa)
      BITSET_SET(b.shader->info.msaa_images, image_dst_index);
   /* The workgroup size varies depending on the tiling layout and blit dimensions. */
   b.shader->info.workgroup_size_variable = true;
   b.shader->info.cs.user_data_components_amd =
      key->is_clear ? (key->d16 ? 6 : 8) : key->has_start_xyz ? 4 : 3;

   const struct glsl_type *img_type[2] = {
      glsl_image_type(key->src_is_1d ? GLSL_SAMPLER_DIM_1D :
                      key->src_is_msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D,
                      key->src_has_z, GLSL_TYPE_FLOAT),
      glsl_image_type(key->dst_is_1d ? GLSL_SAMPLER_DIM_1D :
                      key->dst_is_msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D,
                      key->dst_has_z, GLSL_TYPE_FLOAT),
   };

   nir_variable *img_src = NULL;
   if (!key->is_clear) {
      img_src = nir_variable_create(b.shader, nir_var_uniform, img_type[0], "img0");
      img_src->data.binding = 0;
   }

   nir_variable *img_dst = nir_variable_create(b.shader, nir_var_uniform, img_type[1], "img1");
   img_dst->data.binding = image_dst_index;

   unsigned lane_width = 1 << key->log_lane_width;
   unsigned lane_height = 1 << key->log_lane_height;
   unsigned lane_depth = 1 << key->log_lane_depth;
   unsigned lane_size = lane_width * lane_height * lane_depth;
   assert(lane_size <= SI_MAX_COMPUTE_BLIT_LANE_SIZE);

   nir_def *zero_lod = nir_imm_intN_t(&b, 0, key->a16 ? 16 : 32);

   /* Instructions. */
   /* Let's work with 0-based src and dst coordinates (thread IDs) first. */
   unsigned coord_bit_size = key->a16 ? 16 : 32;
   nir_def *dst_xyz = ac_get_global_ids(&b, key->wg_dim, coord_bit_size);
   dst_xyz = nir_pad_vector_imm_int(&b, dst_xyz, 0, 3);

   /* If the blit area is unaligned, we launched extra threads to make it aligned.
    * Skip those threads here.
    */
   nir_if *if_positive = NULL;
   if (key->has_start_xyz) {
      nir_def *start_xyz = nir_channel(&b, nir_load_user_data_amd(&b), 3);
      start_xyz = nir_u2uN(&b, nir_unpack_32_4x8(&b, start_xyz), coord_bit_size);
      start_xyz = nir_trim_vector(&b, start_xyz, 3);

      dst_xyz = nir_isub(&b, dst_xyz, start_xyz);
      nir_def *is_positive_xyz = nir_ige_imm(&b, dst_xyz, 0);
      nir_def *is_positive = nir_iand(&b, nir_channel(&b, is_positive_xyz, 0),
                                      nir_iand(&b, nir_channel(&b, is_positive_xyz, 1),
                                               nir_channel(&b, is_positive_xyz, 2)));
      if_positive = nir_push_if(&b, is_positive);
   }

   dst_xyz = nir_imul(&b, dst_xyz, nir_imm_ivec3_intN(&b, lane_width, lane_height, lane_depth,
                                                      coord_bit_size));
   nir_def *src_xyz = dst_xyz;

   /* Flip src coordinates. */
   for (unsigned i = 0; i < 2; i++) {
      if (i ? key->flip_y : key->flip_x) {
         /* A normal blit loads from (box.x + tid.x) where tid.x = 0..(width - 1).
          *
          * A flipped blit sets box.x = width, so we should make tid.x negative to load from
          * (width - 1)..0.
          *
          * Therefore do: x = -x - 1, which becomes (width - 1) to 0 after we add box.x = width.
          */
         nir_def *comp = nir_channel(&b, src_xyz, i);
         comp = nir_iadd_imm(&b, nir_ineg(&b, comp), -(int)(i ? lane_height : lane_width));
         src_xyz = nir_vector_insert_imm(&b, src_xyz, comp, i);
      }
   }

   /* Add box.xyz. */
   nir_def *base_coord_src = NULL, *base_coord_dst = NULL;
   unpack_2x16_signed(&b, coord_bit_size, nir_trim_vector(&b, nir_load_user_data_amd(&b), 3),
                      &base_coord_src, &base_coord_dst);
   base_coord_dst = nir_iadd(&b, base_coord_dst, dst_xyz);
   base_coord_src = nir_iadd(&b, base_coord_src, src_xyz);

   /* Coordinates must have 4 channels in NIR. */
   base_coord_src = nir_pad_vector(&b, base_coord_src, 4);
   base_coord_dst = nir_pad_vector(&b, base_coord_dst, 4);

/* Iterate over all pixels in the lane. num_samples is the only input.
 * (sample, x, y, z) are generated coordinates, while "i" is the coordinates converted to
 * an absolute index.
 */
#define foreach_pixel_in_lane(num_samples, sample, x, y, z, i) \
   for (unsigned z = 0; z < lane_depth; z++) \
      for (unsigned y = 0; y < lane_height; y++) \
         for (unsigned x = 0; x < lane_width; x++) \
            for (unsigned i = ((z * lane_height + y) * lane_width + x) * (num_samples), sample = 0; \
                 sample < (num_samples); sample++, i++) \

   /* Swizzle coordinates for 1D_ARRAY. */
   static const unsigned swizzle_xz[] = {0, 2, 0, 0};

   /* Execute image loads and stores. */
   unsigned num_src_coords = (key->src_is_1d ? 1 : 2) + key->src_has_z + key->src_is_msaa;
   unsigned num_dst_coords = (key->dst_is_1d ? 1 : 2) + key->dst_has_z + key->dst_is_msaa;
   unsigned bit_size = key->d16 ? 16 : 32;
   unsigned num_samples = 1 << key->log_samples;
   unsigned src_samples = key->src_is_msaa && !key->sample0_only &&
                          !key->is_clear ? num_samples : 1;
   unsigned dst_samples = key->dst_is_msaa ? num_samples : 1;
   nir_def *color[SI_MAX_COMPUTE_BLIT_LANE_SIZE * SI_MAX_COMPUTE_BLIT_SAMPLES] = {0};
   nir_def *coord_dst[SI_MAX_COMPUTE_BLIT_LANE_SIZE * SI_MAX_COMPUTE_BLIT_SAMPLES] = {0};
   nir_def *src_resinfo = NULL;

   if (key->is_clear) {
      /* The clear color starts at component 4 of user data. */
      color[0] = nir_channels(&b, nir_load_user_data_amd(&b),
                              BITFIELD_RANGE(4, key->d16 ? 2 : 4));
      if (key->d16)
         color[0] = nir_unpack_64_4x16(&b, nir_pack_64_2x32(&b, color[0]));

      foreach_pixel_in_lane(1, sample, x, y, z, i) {
         color[i] = color[0];
      }
   } else {
      nir_def *coord_src[SI_MAX_COMPUTE_BLIT_LANE_SIZE * SI_MAX_COMPUTE_BLIT_SAMPLES] = {0};

      /* Initialize src coordinates, one vector per pixel. */
      foreach_pixel_in_lane(src_samples, sample, x, y, z, i) {
         unsigned tmp_x = x;
         unsigned tmp_y = y;

         /* Change the order from 0..N to N..0 for flipped blits. */
         if (key->flip_x)
            tmp_x = lane_width - 1 - x;
         if (key->flip_y)
            tmp_y = lane_height - 1 - y;

         coord_src[i] = nir_iadd(&b, base_coord_src,
                                     nir_imm_ivec4_intN(&b, tmp_x, tmp_y, z, 0, coord_bit_size));
         if (key->src_is_1d)
            coord_src[i] = nir_swizzle(&b, coord_src[i], swizzle_xz, 4);
         if (key->src_is_msaa) {
            coord_src[i] = nir_vector_insert_imm(&b, coord_src[i],
                                                 nir_imm_intN_t(&b, sample, coord_bit_size),
                                                 num_src_coords - 1);
         }

         /* Clamp to edge for src, only X and Y because Z can't be out of bounds. */
         for (unsigned chan = 0; chan < 2; chan++) {
            if (chan ? key->y_clamp_to_edge : key->x_clamp_to_edge) {
               assert(!key->src_is_1d || chan == 0);

               if (!src_resinfo) {
                  /* Always use the 32-bit return type because the image dimensions can be
                   * > INT16_MAX even if the blit box fits within sint16.
                   */
                  src_resinfo = nir_image_deref_size(&b, 4, 32, deref_ssa(&b, img_src),
                                                     zero_lod);
                  if (coord_bit_size == 16) {
                     src_resinfo = nir_umin_imm(&b, src_resinfo, INT16_MAX);
                     src_resinfo = nir_i2i16(&b, src_resinfo);
                  }
               }

               nir_def *tmp = nir_channel(&b, coord_src[i], chan);
               tmp = nir_imax_imm(&b, tmp, 0);
               tmp = nir_imin(&b, tmp, nir_iadd_imm(&b, nir_channel(&b, src_resinfo, chan), -1));
               coord_src[i] = nir_vector_insert_imm(&b, coord_src[i], tmp, chan);
            }
         }
      }

      /* We don't want the computation of src coordinates to be interleaved with loads. */
      if (lane_size > 1 || src_samples > 1) {
         ac_optimization_barrier_vgpr_array(options->info, &b, coord_src,
                                            lane_size * src_samples, num_src_coords);
      }

      /* Use "samples_identical" for MSAA resolving if it's supported. */
      bool is_resolve = src_samples > 1 && dst_samples == 1;
      bool uses_samples_identical = options->info->gfx_level < GFX11 && !options->no_fmask && is_resolve;
      nir_def *samples_identical = NULL, *sample0[SI_MAX_COMPUTE_BLIT_LANE_SIZE] = {0};
      nir_if *if_identical = NULL;

      if (uses_samples_identical) {
         samples_identical = nir_imm_true(&b);

         /* If we are resolving multiple pixels per lane, AND all results of "samples_identical". */
         foreach_pixel_in_lane(1, sample, x, y, z, i) {
            nir_def *iden = nir_image_deref_samples_identical(&b, 1, deref_ssa(&b, img_src),
                                                              coord_src[i * src_samples],
                                                              .image_dim = GLSL_SAMPLER_DIM_MS);
            samples_identical = nir_iand(&b, samples_identical, iden);
         }

         /* If all samples are identical, load only sample 0. */
         if_identical = nir_push_if(&b, samples_identical);
         foreach_pixel_in_lane(1, sample, x, y, z, i) {
            sample0[i] = nir_image_deref_load(&b, key->last_src_channel + 1, bit_size,
                                              deref_ssa(&b, img_src), coord_src[i * src_samples],
                                              nir_channel(&b, coord_src[i * src_samples],
                                                          num_src_coords - 1), zero_lod,
                                              .image_dim = img_src->type->sampler_dimensionality,
                                              .image_array = img_src->type->sampler_array);
         }
         nir_push_else(&b, if_identical);
      }

      /* Load src pixels, one per sample. */
      foreach_pixel_in_lane(src_samples, sample, x, y, z, i) {
         color[i] = nir_image_deref_load(&b, key->last_src_channel + 1, bit_size,
                                         deref_ssa(&b, img_src), coord_src[i],
                                         nir_channel(&b, coord_src[i], num_src_coords - 1), zero_lod,
                                         .image_dim = img_src->type->sampler_dimensionality,
                                         .image_array = img_src->type->sampler_array);
      }

      /* Resolve MSAA if necessary. */
      if (is_resolve) {
         /* We don't want the averaging of samples to be interleaved with image loads. */
         ac_optimization_barrier_vgpr_array(options->info, &b, color, lane_size * src_samples,
                                            key->last_src_channel + 1);

         /* This reduces the "color" array from "src_samples * lane_size" elements to only
          * "lane_size" elements.
          */
         foreach_pixel_in_lane(1, sample, x, y, z, i) {
            color[i] = ac_average_samples(&b, &color[i * src_samples], src_samples);
         }
         src_samples = 1;
      }

      if (uses_samples_identical) {
         nir_pop_if(&b, if_identical);
         foreach_pixel_in_lane(1, sample, x, y, z, i) {
            color[i] = nir_if_phi(&b, sample0[i], color[i]);
         }
      }
   }

   /* We need to load the descriptor here, otherwise the load would be after optimization
    * barriers waiting for image loads, i.e. after s_waitcnt vmcnt(0).
    */
   nir_def *img_dst_desc =
      nir_image_deref_descriptor_amd(&b, 8, 32, deref_ssa(&b, img_dst),
                                     .image_dim = img_dst->type->sampler_dimensionality,
                                     .image_array = img_dst->type->sampler_array);
   if (lane_size > 1 && !b.shader->info.use_aco_amd)
      img_dst_desc = nir_optimization_barrier_sgpr_amd(&b, 32, img_dst_desc);

   /* Apply the blit output modifiers, once per sample.  */
   foreach_pixel_in_lane(src_samples, sample, x, y, z, i) {
      color[i] = apply_blit_output_modifiers(&b, color[i], key);
   }

   /* Initialize dst coordinates, one vector per pixel. */
   foreach_pixel_in_lane(dst_samples, sample, x, y, z, i) {
      coord_dst[i] = nir_iadd(&b, base_coord_dst,
                              nir_imm_ivec4_intN(&b, x, y, z, 0, coord_bit_size));
      if (key->dst_is_1d)
         coord_dst[i] = nir_swizzle(&b, coord_dst[i], swizzle_xz, 4);
      if (key->dst_is_msaa) {
         coord_dst[i] = nir_vector_insert_imm(&b, coord_dst[i],
                                              nir_imm_intN_t(&b, sample, coord_bit_size),
                                              num_dst_coords - 1);
      }
   }

   /* We don't want the computation of dst coordinates to be interleaved with stores. */
   if (lane_size > 1 || dst_samples > 1) {
      ac_optimization_barrier_vgpr_array(options->info, &b, coord_dst, lane_size * dst_samples,
                                         num_dst_coords);
   }

   /* We don't want the application of blit output modifiers to be interleaved with stores. */
   if (!key->is_clear && (lane_size > 1 || MIN2(src_samples, dst_samples) > 1)) {
      ac_optimization_barrier_vgpr_array(options->info, &b, color, lane_size * src_samples,
                                         key->last_dst_channel + 1);
   }

   /* Store the pixels, one per sample. */
   foreach_pixel_in_lane(dst_samples, sample, x, y, z, i) {
      nir_bindless_image_store(&b, img_dst_desc, coord_dst[i],
                               nir_channel(&b, coord_dst[i], num_dst_coords - 1),
                               src_samples > 1 ? color[i] : color[i / dst_samples], zero_lod,
                               .image_dim = glsl_get_sampler_dim(img_type[1]),
                               .image_array = glsl_sampler_type_is_array(img_type[1]));
   }

   if (key->has_start_xyz)
      nir_pop_if(&b, if_positive);

   return b.shader;
}

static unsigned
set_work_size(struct ac_cs_blit_dispatch *dispatch,
              unsigned block_x, unsigned block_y, unsigned block_z,
              unsigned num_wg_x, unsigned num_wg_y, unsigned num_wg_z)
{
   dispatch->wg_size[0] = block_x;
   dispatch->wg_size[1] = block_y;
   dispatch->wg_size[2] = block_z;

   unsigned num_wg[3] = {num_wg_x, num_wg_y, num_wg_z};
   for (int i = 0; i < 3; ++i) {
      dispatch->last_wg_size[i] = num_wg[i] % dispatch->wg_size[i];
      dispatch->num_workgroups[i] = DIV_ROUND_UP(num_wg[i], dispatch->wg_size[i]);
   }

   return num_wg_z > 1 ? 3 : (num_wg_y > 1 ? 2 : 1);
}

static bool
should_blit_clamp_to_edge(const struct ac_cs_blit_description *blit, unsigned coord_mask)
{
   return util_is_box_out_of_bounds(&blit->src.box, coord_mask, blit->src.width0,
                                    blit->src.height0, blit->src.level);
}

/* Return a power-of-two alignment of a number. */
static unsigned
compute_alignment(unsigned x)
{
   return x ? BITFIELD_BIT(ffs(x) - 1) : BITFIELD_BIT(31);
}

/* Set the blit info, but change the dst box and trim the src box according to the new dst box. */
static void
set_trimmed_blit(const struct ac_cs_blit_description *old, const struct pipe_box *box,
                 bool is_clear, struct ac_cs_blit_description *out)
{
   assert(old->dst.box.x <= box->x);
   assert(old->dst.box.y <= box->y);
   assert(old->dst.box.z <= box->z);
   assert(box->x + box->width <= old->dst.box.x + old->dst.box.width);
   assert(box->y + box->height <= old->dst.box.y + old->dst.box.height);
   assert(box->z + box->depth <= old->dst.box.z + old->dst.box.depth);
   /* No scaling. */
   assert(is_clear || old->dst.box.width == abs(old->src.box.width));
   assert(is_clear || old->dst.box.height == abs(old->src.box.height));
   assert(is_clear || old->dst.box.depth == abs(old->src.box.depth));

   *out = *old;
   out->dst.box = *box;

   if (!is_clear) {
      if (out->src.box.width > 0) {
         out->src.box.x += box->x - old->dst.box.x;
         out->src.box.width = box->width;
      } else {
         out->src.box.x -= box->x - old->dst.box.x;
         out->src.box.width = -box->width;
      }

      if (out->src.box.height > 0) {
         out->src.box.y += box->y - old->dst.box.y;
         out->src.box.height = box->height;
      } else {
         out->src.box.y -= box->y - old->dst.box.y;
         out->src.box.height = -box->height;
      }

      out->src.box.z += box->z - old->dst.box.z;
      out->src.box.depth = box->depth;
   }
}

typedef struct {
   unsigned x, y, z;
} uvec3;

/* This function uses the blit description to generate the shader key, prepare user SGPR constants,
 * and determine the parameters for up to 7 compute dispatches.
 *
 * The driver should use the shader key to create the shader, set the SGPR constants, and launch
 * compute dispatches.
 */
bool
ac_prepare_compute_blit(const struct ac_cs_blit_options *options,
                        const struct ac_cs_blit_description *blit,
                        struct ac_cs_blit_dispatches *out)
{
   const struct radeon_info *info = options->info;
   bool is_2d_tiling = !blit->dst.surf->is_linear && !blit->dst.surf->thick_tiling;
   bool is_3d_tiling = blit->dst.surf->thick_tiling;
   bool is_clear = !blit->src.surf;
   unsigned dst_samples = MAX2(1, blit->dst.num_samples);
   unsigned src_samples = is_clear ? 1 : MAX2(1, blit->src.num_samples);
   bool is_resolve = !is_clear && dst_samples == 1 && src_samples >= 2 &&
                     !util_format_is_pure_integer(blit->dst.format);
   bool is_upsampling = !is_clear && src_samples == 1 && dst_samples >= 2;
   bool sample0_only = src_samples >= 2 && dst_samples == 1 &&
                       (blit->sample0_only || util_format_is_pure_integer(blit->dst.format));
   /* Get the channel sizes. */
   unsigned max_dst_chan_size = util_format_get_max_channel_size(blit->dst.format);
   unsigned max_src_chan_size = is_clear ? 0 : util_format_get_max_channel_size(blit->src.format);

   if (!options->is_nested)
      memset(out, 0, sizeof(*out));

   /* Reject blits with invalid parameters. */
   if (blit->dst.box.width < 0 || blit->dst.box.height < 0 || blit->dst.box.depth < 0 ||
       blit->src.box.depth < 0) {
      assert(!"invalid box parameters"); /* this is reachable and prevents hangs */
      return true;
   }

   /* Skip zero-area blits. */
   if (!blit->dst.box.width || !blit->dst.box.height || !blit->dst.box.depth ||
       (!is_clear && (!blit->src.box.width || !blit->src.box.height || !blit->src.box.depth)))
      return true;

   if (blit->dst.format == PIPE_FORMAT_A8R8_UNORM || /* This format fails AMD_TEST=imagecopy. */
       max_dst_chan_size == 5 || /* PIPE_FORMAT_R5G5B5A1_UNORM has precision issues */
       max_dst_chan_size == 6 || /* PIPE_FORMAT_R5G6B5_UNORM has precision issues */
       util_format_is_depth_or_stencil(blit->dst.format) ||
       dst_samples > SI_MAX_COMPUTE_BLIT_SAMPLES ||
       /* Image stores support DCC since GFX10. Fail only for gfx queues because compute queues
        * can't fall back to a pixel shader. DCC must be decompressed and disabled for compute
        * queues by the caller. */
       (options->info->gfx_level < GFX10 && blit->is_gfx_queue && blit->dst_has_dcc) ||
       (!is_clear &&
        /* Scaling is not implemented by the compute shader. */
        (blit->dst.box.width != abs(blit->src.box.width) ||
         blit->dst.box.height != abs(blit->src.box.height) ||
         blit->dst.box.depth != abs(blit->src.box.depth) ||
         util_format_is_depth_or_stencil(blit->src.format) ||
         src_samples > SI_MAX_COMPUTE_BLIT_SAMPLES)))
      return false;

   /* Return a failure if a compute blit is slower than a gfx blit. */
   if (options->fail_if_slow) {
      if (is_clear) {
         /* Verified on: Tahiti, Hawaii, Tonga, Vega10, Navi10, Navi21, Navi31 */
         if (is_3d_tiling) {
            if (info->gfx_level == GFX6 && blit->dst.surf->bpe == 8)
               return false;
         } else if (is_2d_tiling) {
            if (!(info->gfx_level == GFX6 && blit->dst.surf->bpe <= 4 && dst_samples == 1) &&
                !(info->gfx_level == GFX7 && blit->dst.surf->bpe == 1 && dst_samples == 1))
               return false;
         }
      } else {
         /* For upsampling, image stores don't compress MSAA as good as draws. */
         if (is_upsampling)
            return false;

         switch (info->gfx_level) {
         case GFX6:
         case GFX7:
         case GFX8:
         case GFX9:
         case GFX10:
         case GFX10_3:
            /* Verified on: Tahiti, Hawaii, Tonga, Vega10, Navi10, Navi21 */
            if (is_resolve) {
               if (!(info->gfx_level == GFX7 && blit->dst.surf->bpe == 16))
                  return false;
            } else {
               assert(dst_samples == src_samples || sample0_only);

               if (is_2d_tiling) {
                  if (dst_samples == 1) {
                     if (blit->dst.surf->bpe <= 8 &&
                         !(info->gfx_level <= GFX7 && blit->dst.surf->bpe == 1) &&
                         !(info->gfx_level == GFX6 && blit->dst.surf->bpe == 2 &&
                           blit->src.surf->is_linear) &&
                         !(info->gfx_level == GFX7 && blit->dst.surf->bpe >= 2 &&
                           blit->src.surf->is_linear) &&
                         !((info->gfx_level == GFX8 || info->gfx_level == GFX9) &&
                           blit->dst.surf->bpe >= 2 && blit->src.surf->is_linear) &&
                         !(info->gfx_level == GFX10 && blit->dst.surf->bpe <= 2 &&
                           blit->src.surf->is_linear) &&
                         !(info->gfx_level == GFX10_3 && blit->dst.surf->bpe == 8 &&
                           blit->src.surf->is_linear))
                        return false;

                     if (info->gfx_level == GFX6 && blit->dst.surf->bpe == 16 &&
                         blit->src.surf->is_linear && blit->dst.dim != 3)
                        return false;

                     if (blit->dst.surf->bpe == 16 && !blit->src.surf->is_linear &&
                         /* Only GFX6 selects 2D tiling for 128bpp 3D textures. */
                         !(info->gfx_level == GFX6 && blit->dst.dim == 3) &&
                         info->gfx_level != GFX7)
                        return false;
                  } else {
                     /* MSAA copies - tested only without FMASK on Navi21. */
                     if (blit->dst.surf->bpe >= 4)
                        return false;
                  }
               }
            }
            break;

         case GFX11:
         case GFX11_5:
         default:
            /* Verified on Navi31. */
            if (is_resolve) {
               if (!((blit->dst.surf->bpe <= 2 && src_samples == 2) ||
                     (blit->dst.surf->bpe == 2 && src_samples == 4) ||
                     (blit->dst.surf->bpe == 16 && src_samples == 4)))
                  return false;
            } else {
               assert(dst_samples == src_samples || sample0_only);

               if (is_2d_tiling) {
                  if (blit->dst.surf->bpe == 2 && blit->src.surf->is_linear && dst_samples == 1)
                     return false;

                  if (blit->dst.surf->bpe >= 4 && dst_samples == 1 && !blit->src.surf->is_linear)
                     return false;

                  if (blit->dst.surf->bpe == 16 && dst_samples == 8)
                     return false;
               }
            }
            break;
         }
      }
   }

   unsigned width = blit->dst.box.width;
   unsigned height = blit->dst.box.height;
   unsigned depth = blit->dst.box.depth;
   uvec3 lane_size = (uvec3){1, 1, 1};

   /* Determine the size of the block of pixels that will be processed by a single lane.
    * Generally we want to load and store about 8-16B per lane, but there are exceptions.
    * The block sizes were fine-tuned for Navi31, and might be suboptimal on different generations.
    */
   if (blit->dst.surf->bpe <= 8 && (is_resolve ? src_samples : dst_samples) <= 4 &&
       /* Small blits don't benefit. */
       width * height * depth * blit->dst.surf->bpe * dst_samples > 128 * 1024 &&
       info->has_image_opcodes) {
      if (is_3d_tiling) {
         /* Thick tiling. */
         if (!is_clear && blit->src.surf->is_linear) {
            /* Linear -> Thick. */
            if (blit->dst.surf->bpe == 4)
               lane_size = (uvec3){2, 1, 1}; /* 8B per lane */
            else if (blit->dst.surf->bpe == 2)
               lane_size = (uvec3){2, 1, 2}; /* 8B per lane */
            else if (blit->dst.surf->bpe == 1)
               lane_size = (uvec3){4, 1, 2}; /* 8B per lane */
         } else {
            if (blit->dst.surf->bpe == 8)
               lane_size = (uvec3){1, 1, 2}; /* 16B per lane */
            else if (blit->dst.surf->bpe == 4)
               lane_size = (uvec3){1, 2, 2}; /* 16B per lane */
            else if (blit->dst.surf->bpe == 2)
               lane_size = (uvec3){1, 2, 4}; /* 16B per lane */
            else
               lane_size = (uvec3){2, 2, 2}; /* 8B per lane */
         }
      } else if (blit->dst.surf->is_linear) {
         /* Linear layout. */
         if (!is_clear && !blit->src.surf->is_linear) {
            /* Tiled -> Linear. */
            if (blit->dst.surf->bpe == 8 && !blit->src.surf->thick_tiling)
               lane_size = (uvec3){2, 1, 1}; /* 16B per lane */
            else if (blit->dst.surf->bpe == 4)
               lane_size = (uvec3){1, 2, 1}; /* 8B per lane */
            else if (blit->dst.surf->bpe == 2 && blit->src.surf->thick_tiling)
               lane_size = (uvec3){2, 2, 1}; /* 8B per lane */
            else if (blit->dst.surf->bpe == 1 && blit->src.surf->thick_tiling)
               lane_size = (uvec3){2, 2, 2}; /* 8B per lane */
            else if (blit->dst.surf->bpe <= 2)
               lane_size = (uvec3){2, 4, 1}; /* 8-16B per lane */
         } else {
            /* Clear or Linear -> Linear. */
            if (blit->dst.surf->bpe == 8)
               lane_size = (uvec3){2, 1, 1}; /* 16B per lane */
            else if (blit->dst.surf->bpe == 4)
               lane_size = (uvec3){4, 1, 1}; /* 16B per lane */
            else if (blit->dst.surf->bpe == 2)
               lane_size = (uvec3){4, 2, 1}; /* 16B per lane */
            else
               lane_size = (uvec3){8, 1, 1}; /* 8B per lane */
         }
      } else {
         /* Thin tiling. */
         if (is_resolve) {
            if (blit->dst.surf->bpe == 8 && src_samples == 2) {
               lane_size = (uvec3){1, 2, 1}; /* 32B->16B per lane */
            } else if (blit->dst.surf->bpe == 4) {
               lane_size = (uvec3){2, 1, 1}; /* 32B->8B for 4 samples, 16B->8B for 2 samples */
            } else if (blit->dst.surf->bpe <= 2) {
               if (src_samples == 4)
                  lane_size = (uvec3){2, 1, 1}; /* 16B->4B for 16bpp, 8B->2B for 8bpp */
               else
                  lane_size = (uvec3){2, 2, 1}; /* 16B->8B for 16bpp, 8B->4B for 8bpp */
            }
         } else {
            if (blit->dst.surf->bpe == 8 && dst_samples == 1)
               lane_size = (uvec3){1, 2, 1}; /* 16B per lane */
            else if (blit->dst.surf->bpe == 4) {
               if (dst_samples == 2)
                  lane_size = (uvec3){2, 1, 1}; /* 16B per lane */
               else if (dst_samples == 1)
                  lane_size = (uvec3){2, 2, 1}; /* 16B per lane */
            } else if (blit->dst.surf->bpe == 2) {
               if (dst_samples == 4 || (!is_clear && blit->src.surf->is_linear))
                  lane_size = (uvec3){2, 1, 1}; /* 16B per lane (4B for linear src) */
               else if (dst_samples == 2)
                  lane_size = (uvec3){2, 2, 1}; /* 16B per lane */
               else
                  lane_size = (uvec3){2, 4, 1}; /* 16B per lane */
            } else if (blit->dst.surf->bpe == 1) {
               if (dst_samples == 4)
                  lane_size = (uvec3){2, 1, 1}; /* 8B per lane */
               else if (dst_samples == 2 || (!is_clear && blit->src.surf->is_linear))
                  lane_size = (uvec3){2, 2, 1}; /* 8B per lane (4B for linear src) */
               else
                  lane_size = (uvec3){2, 4, 1}; /* 8B per lane */
            }
         }
      }
   }

   /* Check that the lane size fits into the shader key. */
   static const union ac_cs_blit_key max_lane_size = {
      .log_lane_width = ~0,
      .log_lane_height = ~0,
      .log_lane_depth = ~0,
   };
   assert(util_logbase2(lane_size.x) <= max_lane_size.log_lane_width);
   assert(util_logbase2(lane_size.y) <= max_lane_size.log_lane_height);
   assert(util_logbase2(lane_size.z) <= max_lane_size.log_lane_depth);

   /* If the shader blits a block of pixels per lane, it must have the dst box aligned to that
    * block because it can't blit a subset of pixels per lane.
    *
    * If the blit dst box is not aligned to the lane size, split it into multiple blits by cutting
    * off the unaligned sides of the box and blitting the middle that's aligned to the lane size,
    * then blit the unaligned sides separately. This splits the blit into up to 7 blits for 3D,
    * and 5 blits for 2D.
    */
   if (blit->dst.box.x % lane_size.x ||
       blit->dst.box.y % lane_size.y ||
       blit->dst.box.z % lane_size.z ||
       blit->dst.box.width % lane_size.x ||
       blit->dst.box.height % lane_size.y ||
       blit->dst.box.depth % lane_size.z) {
      struct pipe_box middle;

      /* Cut off unaligned regions on the sides of the box. */
      middle.x = align(blit->dst.box.x, lane_size.x);
      middle.y = align(blit->dst.box.y, lane_size.y);
      middle.z = align(blit->dst.box.z, lane_size.z);

      middle.width = blit->dst.box.width - (middle.x - blit->dst.box.x);
      if (middle.width > 0)
         middle.width -= middle.width % lane_size.x;
      middle.height = blit->dst.box.height - (middle.y - blit->dst.box.y);
      if (middle.height > 0)
         middle.height -= middle.height % lane_size.y;
      middle.depth = blit->dst.box.depth - (middle.z - blit->dst.box.z);
      if (middle.depth > 0)
         middle.depth -= middle.depth % lane_size.z;

      /* Only a few cases are regressed by this. The vast majority benefits a lot.
       * This was fine-tuned for Navi31, and might be suboptimal on different generations.
       */
      bool slow = (blit->dst.surf->is_linear && !is_clear && blit->src.surf->is_linear && depth > 1) ||
                  (blit->dst.surf->thick_tiling &&
                   ((blit->dst.surf->bpe == 8 && is_clear) ||
                    (blit->dst.surf->bpe == 4 &&
                     (blit->dst.surf->is_linear || (!is_clear && blit->src.surf->is_linear))) ||
                    (blit->dst.surf->bpe == 2 && blit->dst.surf->is_linear && !is_clear &&
                     blit->src.surf->is_linear))) ||
                  (!blit->dst.surf->thick_tiling &&
                   ((blit->dst.surf->bpe == 4 && blit->dst.surf->is_linear && !is_clear &&
                     blit->src.surf->is_linear) ||
                    (blit->dst.surf->bpe == 8 && !is_clear &&
                     blit->dst.surf->is_linear != blit->src.surf->is_linear) ||
                    (is_resolve && blit->dst.surf->bpe == 4 && src_samples == 4) ||
                    (is_resolve && blit->dst.surf->bpe == 8 && src_samples == 2)));

      /* Only use this if the middle blit is large enough. */
      if (!slow && middle.width > 0 && middle.height > 0 && middle.depth > 0 &&
          middle.width * middle.height * middle.depth * blit->dst.surf->bpe * dst_samples >
          128 * 1024) {
         /* Compute the size of unaligned regions on all sides of the box. */
         struct pipe_box top, left, right, bottom, front, back;

         assert(!options->is_nested);

         top = blit->dst.box;
         top.height = middle.y - top.y;

         bottom = blit->dst.box;
         bottom.y = middle.y + middle.height;
         bottom.height = blit->dst.box.height - top.height - middle.height;

         left = blit->dst.box;
         left.y = middle.y;
         left.height = middle.height;
         left.width = middle.x - left.x;

         right = blit->dst.box;
         right.y = middle.y;
         right.height = middle.height;
         right.x = middle.x + middle.width;
         right.width = blit->dst.box.width - left.width - middle.width;

         front = blit->dst.box;
         front.x = middle.x;
         front.y = middle.y;
         front.width = middle.width;
         front.height = middle.height;
         front.depth = middle.z - front.z;

         back = blit->dst.box;
         back.x = middle.x;
         back.y = middle.y;
         back.width = middle.width;
         back.height = middle.height;
         back.z = middle.z + middle.depth;
         back.depth = blit->dst.box.depth - front.depth - middle.depth;

         struct pipe_box boxes[] = {middle, top, bottom, left, right, front, back};

         /* Verify that the boxes don't intersect. */
         for (unsigned i = 0; i < ARRAY_SIZE(boxes); i++) {
            for (unsigned j = i + 1; j < ARRAY_SIZE(boxes); j++) {
               if (boxes[i].width > 0 && boxes[i].height > 0 && boxes[i].depth > 0 &&
                   boxes[j].width > 0 && boxes[j].height > 0 && boxes[j].depth > 0) {
                  if (u_box_test_intersection_3d(&boxes[i], &boxes[j])) {
                     printf("\b   (%u, %u, %u) -> (%u, %u, %u) | (%u, %u, %u) -> (%u, %u, %u)\n",
                            boxes[i].x, boxes[i].y, boxes[i].z,
                            boxes[i].x + boxes[i].width - 1,
                            boxes[i].y + boxes[i].height - 1,
                            boxes[i].z + boxes[i].depth - 1,
                            boxes[j].x, boxes[j].y, boxes[j].z,
                            boxes[j].x + boxes[j].width,
                            boxes[j].y + boxes[j].height,
                            boxes[j].z + boxes[j].depth);
                     assert(0);
                  }
               }
            }
         }

         struct ac_cs_blit_options nested_options = *options;
         nested_options.is_nested = true;

         for (unsigned i = 0; i < ARRAY_SIZE(boxes); i++) {
            if (boxes[i].width > 0 && boxes[i].height > 0 && boxes[i].depth > 0) {
               struct ac_cs_blit_description new_blit;
               ASSERTED bool ok;

               set_trimmed_blit(blit, &boxes[i], is_clear, &new_blit);
               ok = ac_prepare_compute_blit(&nested_options, &new_blit, out);
               assert(ok);
            }
         }
         return true;
      }
   }

   /* If the box can't blit split, at least reduce the lane size to the alignment of the box. */
   lane_size.x = MIN3(lane_size.x, compute_alignment(blit->dst.box.x), compute_alignment(width));
   lane_size.y = MIN3(lane_size.y, compute_alignment(blit->dst.box.y), compute_alignment(height));
   lane_size.z = MIN3(lane_size.z, compute_alignment(blit->dst.box.z), compute_alignment(depth));

   /* Determine the alignment of coordinates of the first thread of each wave. The alignment should be
    * to a 256B block or the size of 1 wave, whichever is less, but there are a few exceptions.
    */
   uvec3 align;
   if (is_3d_tiling) {
      /* Thick tiling. */
      /* This is based on GFX11_SW_PATTERN_NIBBLE01, which also matches GFX10. */
      if (blit->dst.surf->bpe == 1)
         align = (uvec3){8, 4, 8};
      else if (blit->dst.surf->bpe == 2)
         align = (uvec3){4, 4, 8};
      else if (blit->dst.surf->bpe == 4)
         align = (uvec3){4, 4, 4};
      else if (blit->dst.surf->bpe == 8)
         align = (uvec3){4, 2, 4};
      else {
         /* 16bpp linear source image reads perform better with this. */
         if (!is_clear && blit->src.surf->is_linear)
            align = (uvec3){4, 2, 4}; /* align to 512B for linear->tiled */
         else
            align = (uvec3){2, 2, 4};
      }

      /* Clamp the alignment to the expected size of 1 wave. */
      align.x = MIN2(align.x, 4 * lane_size.x);
      align.y = MIN2(align.y, 4 * lane_size.y);
      align.z = MIN2(align.z, 4 * lane_size.z);
   } else if (blit->dst.surf->is_linear) {
      /* 1D blits from linear to linear are faster unaligned.
       * 1D image clears don't benefit from any alignment.
       */
      if (height == 1 && depth == 1 && (is_clear || blit->src.surf->is_linear)) {
         align = (uvec3){1, 1, 1};
      } else {
         /* Linear blits should use the cache line size instead of 256B alignment.
          * Clamp it to the expected size of 1 wave.
          */
         align.x = MIN2(options->info->tcc_cache_line_size / blit->dst.surf->bpe, 64 * lane_size.x);
         align.y = 1;
         align.z = 1;
      }
   } else {
      /* Thin tiling. */
      if (info->gfx_level >= GFX11) {
         /* Samples are next to each other on GFX11+. */
         unsigned pix_size = blit->dst.surf->bpe * dst_samples;

         /* This is based on GFX11_SW_PATTERN_NIBBLE01. */
         if (pix_size == 1)
            align = (uvec3){16, 16, 1};
         else if (pix_size == 2)
            align = (uvec3){16, 8, 1};
         else if (pix_size == 4)
            align = (uvec3){8, 8, 1};
         else if (pix_size == 8)
            align = (uvec3){8, 4, 1};
         else if (pix_size == 16)
            align = (uvec3){4, 4, 1};
         else if (pix_size == 32)
            align = (uvec3){4, 2, 1};
         else if (pix_size == 64)
            align = (uvec3){2, 2, 1};
         else
            align = (uvec3){2, 1, 1}; /* 16bpp 8xAA */
      } else {
         /* This is for 64KB_R_X. (most likely to occur due to DCC)
          * It's based on GFX10_SW_64K_R_X_*xaa_RBPLUS_PATINFO (GFX10.3).
          * The patterns are GFX10_SW_PATTERN_NIBBLE01[0, 1, 39, 6, 7] for 8bpp-128bpp.
          * GFX6-10.1 and other swizzle modes might be similar.
          */
         if (blit->dst.surf->bpe == 1)
            align = (uvec3){16, 16, 1};
         else if (blit->dst.surf->bpe == 2)
            align = (uvec3){16, 8, 1};
         else if (blit->dst.surf->bpe == 4)
            align = (uvec3){8, 8, 1};
         else if (blit->dst.surf->bpe == 8)
            align = (uvec3){8, 4, 1};
         else
            align = (uvec3){4, 4, 1};
      }

      /* Clamp the alignment to the expected size of 1 wave. */
      align.x = MIN2(align.x, 8 * lane_size.x);
      align.y = MIN2(align.y, 8 * lane_size.y);
   }

   /* If we don't have much to copy, don't align. The threshold is guessed and isn't covered
    * by benchmarking.
    */
   if (width <= align.x * 4)
      align.x = 1;
   if (height <= align.y * 4)
      align.y = 1;
   if (depth <= align.z * 4)
      align.z = 1;

   unsigned start_x, start_y, start_z;
   unsigned block_x, block_y, block_z;

   /* If the blit destination area is unaligned, launch extra threads before 0,0,0 to make it
    * aligned. This makes sure that a wave doesn't straddle a DCC block boundary or a cache line
    * unnecessarily, so that each cache line is only stored by exactly 1 CU. The shader will skip
    * the extra threads. This makes unaligned compute blits faster.
    */
   start_x = blit->dst.box.x % align.x;
   start_y = blit->dst.box.y % align.y;
   start_z = blit->dst.box.z % align.z;
   width += start_x;
   height += start_y;
   depth += start_z;

   /* Divide by the dispatch parameters by the lane size. */
   assert(start_x % lane_size.x == 0);
   assert(start_y % lane_size.y == 0);
   assert(start_z % lane_size.z == 0);
   assert(width % lane_size.x == 0);
   assert(height % lane_size.y == 0);
   assert(depth % lane_size.z == 0);

   start_x /= lane_size.x;
   start_y /= lane_size.y;
   start_z /= lane_size.z;
   width /= lane_size.x;
   height /= lane_size.y;
   depth /= lane_size.z;

   /* Choose the block (i.e. wave) dimensions based on the copy area size and the image layout
    * of dst.
    */
   if (is_3d_tiling) {
      /* Thick tiling. (microtiles are 3D boxes)
       * If the box height and depth is > 2, the block size will be 4x4x4.
       * If not, the threads will spill over to X.
       */
      block_y = util_next_power_of_two(MIN2(height, 4));
      block_z = util_next_power_of_two(MIN2(depth, 4));
      block_x = 64 / (block_y * block_z);
   } else if (blit->dst.surf->is_linear) {
      /* If the box width is > 128B, the block size will be 64x1 for bpp <= 4, 32x2 for bpp == 8,
       * and 16x4 for bpp == 16.
       * If not, the threads will spill over to Y, then Z if they aren't small.
       *
       * This is derived from the fact that the linear image layout has 256B linear blocks, and
       * longer blocks don't benefit linear write performance, but they hurt tiled read performance.
       * We want to prioritize blocks that are 256Bx2 over 512Bx1 because the source can be tiled.
       *
       * Using the cache line size (128B) instead of hardcoding 256B makes linear blits slower.
       */
      block_x = util_next_power_of_two(MIN3(width, 64, 256 / blit->dst.surf->bpe));
      block_y = util_next_power_of_two(MIN2(height, 64 / block_x));
      block_z = util_next_power_of_two(MIN2(depth, 64 / (block_x * block_y)));
      block_x = 64 / (block_y * block_z);
   } else {
      /* Thin tiling. (microtiles are 2D rectangles)
       * If the box width and height is > 4, the block size will be 8x8.
       * If Y is <= 4, the threads will spill over to X.
       * If X is <= 4, the threads will spill over to Y, then Z if they aren't small.
       */
      block_y = util_next_power_of_two(MIN2(height, 8));
      block_x = util_next_power_of_two(MIN2(width, 64 / block_y));
      block_y = util_next_power_of_two(MIN2(height, 64 / block_x));
      block_z = util_next_power_of_two(MIN2(depth, 64 / (block_x * block_y)));
      block_x = 64 / (block_y * block_z);
   }

   unsigned index = out->num_dispatches++;
   assert(index < ARRAY_SIZE(out->dispatches));
   struct ac_cs_blit_dispatch *dispatch = &out->dispatches[index];
   unsigned wg_dim = set_work_size(dispatch, block_x, block_y, block_z, width, height, depth);

   /* Get the shader key. */
   union ac_cs_blit_key key;
   key.key = 0;

   /* Only ACO can form VMEM clauses for image stores, which is a requirement for performance. */
   key.use_aco = true;
   key.is_clear = is_clear;
   key.wg_dim = wg_dim;
   key.has_start_xyz = start_x || start_y || start_z;
   key.log_lane_width = util_logbase2(lane_size.x);
   key.log_lane_height = util_logbase2(lane_size.y);
   key.log_lane_depth = util_logbase2(lane_size.z);
   key.dst_is_1d = blit->dst.dim == 1;
   key.dst_is_msaa = dst_samples > 1;
   key.dst_has_z = blit->dst.dim == 3 || blit->dst.is_array;
   key.last_dst_channel = util_format_get_last_component(blit->dst.format);

   /* ACO doesn't support D16 on GFX8 */
   bool has_d16 = info->gfx_level >= (key.use_aco || options->use_aco ? GFX9 : GFX8);

   if (is_clear) {
      assert(dst_samples <= 8);
      key.log_samples = util_logbase2(dst_samples);
      key.a16 = info->gfx_level >= GFX9 && util_is_box_sint16(&blit->dst.box);
      key.d16 = has_d16 &&
                max_dst_chan_size <= (util_format_is_float(blit->dst.format) ||
                                      util_format_is_pure_integer(blit->dst.format) ? 16 : 11);
   } else {
      key.src_is_1d = blit->src.dim == 1;
      key.src_is_msaa = src_samples > 1;
      key.src_has_z = blit->src.dim == 3 || blit->src.is_array;
      /* Resolving integer formats only copies sample 0. log_samples is then unused. */
      key.sample0_only = sample0_only;
      unsigned num_samples = MAX2(src_samples, dst_samples);
      assert(num_samples <= 8);
      key.log_samples = sample0_only ? 0 : util_logbase2(num_samples);
      key.x_clamp_to_edge = should_blit_clamp_to_edge(blit, BITFIELD_BIT(0));
      key.y_clamp_to_edge = should_blit_clamp_to_edge(blit, BITFIELD_BIT(1));
      key.flip_x = blit->src.box.width < 0;
      key.flip_y = blit->src.box.height < 0;
      key.sint_to_uint = util_format_is_pure_sint(blit->src.format) &&
                         util_format_is_pure_uint(blit->dst.format);
      key.uint_to_sint = util_format_is_pure_uint(blit->src.format) &&
                         util_format_is_pure_sint(blit->dst.format);
      key.dst_is_srgb = util_format_is_srgb(blit->dst.format);
      key.last_src_channel = MIN2(util_format_get_last_component(blit->src.format),
                                  key.last_dst_channel);
      key.use_integer_one = util_format_is_pure_integer(blit->dst.format) &&
                            key.last_src_channel < key.last_dst_channel &&
                            key.last_dst_channel == 3;
      key.a16 = info->gfx_level >= GFX9 && util_is_box_sint16(&blit->dst.box) &&
                util_is_box_sint16(&blit->src.box);
      key.d16 = has_d16 &&
                /* Blitting FP16 using D16 has precision issues. Resolving has precision
                 * issues all the way down to R11G11B10_FLOAT. */
                MIN2(max_dst_chan_size, max_src_chan_size) <=
                (util_format_is_pure_integer(blit->dst.format) ?
                    (key.sint_to_uint || key.uint_to_sint ? 10 : 16) :
                    (is_resolve ? 10 : 11));
   }

   dispatch->shader_key = key;

   dispatch->user_data[0] = (blit->src.box.x & 0xffff) | ((blit->dst.box.x & 0xffff) << 16);
   dispatch->user_data[1] = (blit->src.box.y & 0xffff) | ((blit->dst.box.y & 0xffff) << 16);
   dispatch->user_data[2] = (blit->src.box.z & 0xffff) | ((blit->dst.box.z & 0xffff) << 16);
   dispatch->user_data[3] = (start_x & 0xff) | ((start_y & 0xff) << 8) | ((start_z & 0xff) << 16);

   if (is_clear) {
      union pipe_color_union final_value;
      memcpy(&final_value, &blit->clear_color, sizeof(final_value));

      /* Do the conversion to sRGB here instead of the shader. */
      if (util_format_is_srgb(blit->dst.format)) {
         for (int i = 0; i < 3; i++)
            final_value.f[i] = util_format_linear_to_srgb_float(final_value.f[i]);
      }

      if (key.d16) {
         enum pipe_format data_format;

         if (util_format_is_pure_uint(blit->dst.format))
            data_format = PIPE_FORMAT_R16G16B16A16_UINT;
         else if (util_format_is_pure_sint(blit->dst.format))
            data_format = PIPE_FORMAT_R16G16B16A16_SINT;
         else
            data_format = PIPE_FORMAT_R16G16B16A16_FLOAT;

         util_pack_color_union(data_format, (union util_color *)&dispatch->user_data[4],
                               &final_value);
      } else {
         memcpy(&dispatch->user_data[4], &final_value, sizeof(final_value));
      }
   }

   return true;
}
