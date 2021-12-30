/*
 * Copyright © 2021 Google
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>

#include "nir/nir_builder.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "sid.h"
#include "vk_format.h"

/* Based on
 * https://github.com/Themaister/Granite/blob/master/assets/shaders/decode/etc2.comp
 * https://github.com/Themaister/Granite/blob/master/assets/shaders/decode/eac.comp
 *
 * With some differences:
 *  - Use the vk format to do all the settings.
 *  - Combine the ETC2 and EAC shaders.
 *  - Since we combined the above, reuse the function for the ETC2 A8 component.
 *  - the EAC shader doesn't do SNORM correctly, so this has that fixed.
 */

static nir_ssa_def *
flip_endian(nir_builder *b, nir_ssa_def *src, unsigned cnt)
{
   nir_ssa_def *v[2];
   for (unsigned i = 0; i < cnt; ++i) {
      nir_ssa_def *intermediate[4];
      nir_ssa_def *chan = cnt == 1 ? src : nir_channel(b, src, i);
      for (unsigned j = 0; j < 4; ++j)
         intermediate[j] = nir_ubfe(b, chan, nir_imm_int(b, 8 * j), nir_imm_int(b, 8));
      v[i] = nir_ior(b,
                     nir_ior(b, nir_ishl(b, intermediate[0], nir_imm_int(b, 24)),
                             nir_ishl(b, intermediate[1], nir_imm_int(b, 16))),
                     nir_ior(b, nir_ishl(b, intermediate[2], nir_imm_int(b, 8)),
                             nir_ishl(b, intermediate[3], nir_imm_int(b, 0))));
   }
   return cnt == 1 ? v[0] : nir_vec(b, v, cnt);
}

static nir_ssa_def *
etc1_color_modifier_lookup(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   const unsigned table[8][2] = {{2, 8},   {5, 17},  {9, 29},   {13, 42},
                                 {18, 60}, {24, 80}, {33, 106}, {47, 183}};
   nir_ssa_def *upper = nir_ieq(b, y, nir_imm_int(b, 1));
   nir_ssa_def *result = NULL;
   for (unsigned i = 0; i < 8; ++i) {
      nir_ssa_def *tmp =
         nir_bcsel(b, upper, nir_imm_int(b, table[i][1]), nir_imm_int(b, table[i][0]));
      if (result)
         result = nir_bcsel(b, nir_ieq(b, x, nir_imm_int(b, i)), tmp, result);
      else
         result = tmp;
   }
   return result;
}

static nir_ssa_def *
etc2_distance_lookup(nir_builder *b, nir_ssa_def *x)
{
   const unsigned table[8] = {3, 6, 11, 16, 23, 32, 41, 64};
   nir_ssa_def *result = NULL;
   for (unsigned i = 0; i < 8; ++i) {
      if (result)
         result = nir_bcsel(b, nir_ieq(b, x, nir_imm_int(b, i)), nir_imm_int(b, table[i]), result);
      else
         result = nir_imm_int(b, table[i]);
   }
   return result;
}

static nir_ssa_def *
etc1_alpha_modifier_lookup(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   const unsigned table[16] = {0xe852, 0xc962, 0xc741, 0xc531, 0xb752, 0xa862, 0xa763, 0xa742,
                               0x9751, 0x9741, 0x9731, 0x9641, 0x9632, 0x9210, 0x8753, 0x8642};
   nir_ssa_def *result = NULL;
   for (unsigned i = 0; i < 16; ++i) {
      nir_ssa_def *tmp = nir_imm_int(b, table[i]);
      if (result)
         result = nir_bcsel(b, nir_ieq(b, x, nir_imm_int(b, i)), tmp, result);
      else
         result = tmp;
   }
   return nir_ubfe(b, result, nir_imul(b, y, nir_imm_int(b, 4)), nir_imm_int(b, 4));
}

static nir_ssa_def *
etc_extend(nir_builder *b, nir_ssa_def *v, int bits)
{
   if (bits == 4)
      return nir_imul(b, v, nir_imm_int(b, 0x11));
   return nir_ior(b, nir_ishl(b, v, nir_imm_int(b, 8 - bits)),
                  nir_ushr(b, v, nir_imm_int(b, bits - (8 - bits))));
}

static nir_ssa_def *
decode_etc2_alpha(struct nir_builder *b, nir_ssa_def *alpha_payload, nir_ssa_def *linear_pixel,
                  bool eac, nir_ssa_def *is_signed)
{
   alpha_payload = flip_endian(b, alpha_payload, 2);
   nir_ssa_def *alpha_x = nir_channel(b, alpha_payload, 1);
   nir_ssa_def *alpha_y = nir_channel(b, alpha_payload, 0);
   nir_ssa_def *bit_offset =
      nir_isub(b, nir_imm_int(b, 45), nir_imul(b, nir_imm_int(b, 3), linear_pixel));
   nir_ssa_def *base = nir_ubfe(b, alpha_y, nir_imm_int(b, 24), nir_imm_int(b, 8));
   nir_ssa_def *multiplier = nir_ubfe(b, alpha_y, nir_imm_int(b, 20), nir_imm_int(b, 4));
   nir_ssa_def *table = nir_ubfe(b, alpha_y, nir_imm_int(b, 16), nir_imm_int(b, 4));

   if (eac) {
      nir_ssa_def *signed_base = nir_ibfe(b, alpha_y, nir_imm_int(b, 24), nir_imm_int(b, 8));
      signed_base = nir_imul(b, signed_base, nir_imm_int(b, 8));
      base = nir_iadd(b, nir_imul(b, base, nir_imm_int(b, 8)), nir_imm_int(b, 4));
      base = nir_bcsel(b, is_signed, signed_base, base);
      multiplier = nir_imax(b, nir_imul(b, multiplier, nir_imm_int(b, 8)), nir_imm_int(b, 1));
   }

   nir_ssa_def *lsb_index =
      nir_ubfe(b, nir_bcsel(b, nir_uge(b, bit_offset, nir_imm_int(b, 32)), alpha_y, alpha_x),
               nir_iand(b, bit_offset, nir_imm_int(b, 31)), nir_imm_int(b, 2));
   bit_offset = nir_iadd(b, bit_offset, nir_imm_int(b, 2));
   nir_ssa_def *msb =
      nir_ubfe(b, nir_bcsel(b, nir_uge(b, bit_offset, nir_imm_int(b, 32)), alpha_y, alpha_x),
               nir_iand(b, bit_offset, nir_imm_int(b, 31)), nir_imm_int(b, 1));
   nir_ssa_def *mod = nir_ixor(b, etc1_alpha_modifier_lookup(b, table, lsb_index),
                               nir_isub(b, msb, nir_imm_int(b, 1)));
   nir_ssa_def *a = nir_iadd(b, base, nir_imul(b, mod, multiplier));

   nir_ssa_def *low_bound = nir_imm_int(b, 0);
   nir_ssa_def *high_bound = nir_imm_int(b, 255);
   nir_ssa_def *final_mult = nir_imm_float(b, 1 / 255.0);
   if (eac) {
      low_bound = nir_bcsel(b, is_signed, nir_imm_int(b, -1023), low_bound);
      high_bound = nir_bcsel(b, is_signed, nir_imm_int(b, 1023), nir_imm_int(b, 2047));
      final_mult =
         nir_bcsel(b, is_signed, nir_imm_float(b, 1 / 1023.0), nir_imm_float(b, 1 / 2047.0));
   }

   return nir_fmul(b, nir_i2f32(b, nir_iclamp(b, a, low_bound, high_bound)), final_mult);
}

static nir_shader *
build_shader(struct radv_device *dev)
{
   const struct glsl_type *sampler_type_2d =
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, true, GLSL_TYPE_FLOAT);
   const struct glsl_type *sampler_type_3d =
      glsl_sampler_type(GLSL_SAMPLER_DIM_3D, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type_2d =
      glsl_image_type(GLSL_SAMPLER_DIM_2D, true, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type_3d =
      glsl_image_type(GLSL_SAMPLER_DIM_3D, false, GLSL_TYPE_FLOAT);
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "meta_decode_etc");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   nir_variable *input_img_2d =
      nir_variable_create(b.shader, nir_var_uniform, sampler_type_2d, "s_tex_2d");
   input_img_2d->data.descriptor_set = 0;
   input_img_2d->data.binding = 0;

   nir_variable *input_img_3d =
      nir_variable_create(b.shader, nir_var_uniform, sampler_type_3d, "s_tex_3d");
   input_img_2d->data.descriptor_set = 0;
   input_img_2d->data.binding = 0;

   nir_variable *output_img_2d =
      nir_variable_create(b.shader, nir_var_image, img_type_2d, "out_img_2d");
   output_img_2d->data.descriptor_set = 0;
   output_img_2d->data.binding = 1;

   nir_variable *output_img_3d =
      nir_variable_create(b.shader, nir_var_image, img_type_3d, "out_img_3d");
   output_img_3d->data.descriptor_set = 0;
   output_img_3d->data.binding = 1;

   nir_ssa_def *global_id = get_global_ids(&b, 3);

   nir_ssa_def *consts = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_ssa_def *consts2 =
      nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 0, .range = 4);
   nir_ssa_def *offset = nir_channels(&b, consts, 7);
   nir_ssa_def *format = nir_channel(&b, consts, 3);
   nir_ssa_def *image_type = nir_channel(&b, consts2, 0);
   nir_ssa_def *is_3d = nir_ieq(&b, image_type, nir_imm_int(&b, VK_IMAGE_TYPE_3D));
   nir_ssa_def *coord = nir_iadd(&b, global_id, offset);
   nir_ssa_def *src_coord =
      nir_vec3(&b, nir_ushr_imm(&b, nir_channel(&b, coord, 0), 2),
               nir_ushr_imm(&b, nir_channel(&b, coord, 1), 2), nir_channel(&b, coord, 2));

   nir_variable *payload_var =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "payload");
   nir_push_if(&b, is_3d);
   {
      nir_ssa_def *tex_deref = &nir_build_deref_var(&b, input_img_3d)->dest.ssa;

      nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
      tex->sampler_dim = GLSL_SAMPLER_DIM_3D;
      tex->op = nir_texop_txf;
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[0].src = nir_src_for_ssa(src_coord);
      tex->src[1].src_type = nir_tex_src_lod;
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
      tex->src[2].src_type = nir_tex_src_texture_deref;
      tex->src[2].src = nir_src_for_ssa(tex_deref);
      tex->dest_type = nir_type_uint32;
      tex->is_array = false;
      tex->coord_components = 3;

      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
      nir_builder_instr_insert(&b, &tex->instr);
      nir_store_var(&b, payload_var, &tex->dest.ssa, 0xf);
   }
   nir_push_else(&b, NULL);
   {
      nir_ssa_def *tex_deref = &nir_build_deref_var(&b, input_img_2d)->dest.ssa;

      nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
      tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
      tex->op = nir_texop_txf;
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[0].src = nir_src_for_ssa(src_coord);
      tex->src[1].src_type = nir_tex_src_lod;
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
      tex->src[2].src_type = nir_tex_src_texture_deref;
      tex->src[2].src = nir_src_for_ssa(tex_deref);
      tex->dest_type = nir_type_uint32;
      tex->is_array = true;
      tex->coord_components = 3;

      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
      nir_builder_instr_insert(&b, &tex->instr);
      nir_store_var(&b, payload_var, &tex->dest.ssa, 0xf);
   }
   nir_pop_if(&b, NULL);

   nir_ssa_def *pixel_coord = nir_iand(&b, nir_channels(&b, coord, 3), nir_imm_ivec2(&b, 3, 3));
   nir_ssa_def *linear_pixel =
      nir_iadd(&b, nir_imul(&b, nir_channel(&b, pixel_coord, 0), nir_imm_int(&b, 4)),
               nir_channel(&b, pixel_coord, 1));

   nir_ssa_def *payload = nir_load_var(&b, payload_var);
   nir_variable *color =
      nir_variable_create(b.shader, nir_var_shader_temp, glsl_vec4_type(), "color");
   nir_store_var(&b, color, nir_imm_vec4(&b, 1.0, 0.0, 0.0, 1.0), 0xf);
   nir_push_if(&b, nir_ilt(&b, format, nir_imm_int(&b, VK_FORMAT_EAC_R11_UNORM_BLOCK)));
   {
      nir_ssa_def *alpha_bits_8 =
         nir_ige(&b, format, nir_imm_int(&b, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK));
      nir_ssa_def *alpha_bits_1 =
         nir_iand(&b, nir_ige(&b, format, nir_imm_int(&b, VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK)),
                  nir_ilt(&b, format, nir_imm_int(&b, VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK)));

      nir_ssa_def *color_payload =
         nir_bcsel(&b, alpha_bits_8, nir_channels(&b, payload, 0xC), nir_channels(&b, payload, 3));
      color_payload = flip_endian(&b, color_payload, 2);
      nir_ssa_def *color_y = nir_channel(&b, color_payload, 0);
      nir_ssa_def *color_x = nir_channel(&b, color_payload, 1);
      nir_ssa_def *flip =
         nir_ine(&b, nir_iand(&b, color_y, nir_imm_int(&b, 1)), nir_imm_int(&b, 0));
      nir_ssa_def *subblock = nir_ushr_imm(
         &b, nir_bcsel(&b, flip, nir_channel(&b, pixel_coord, 1), nir_channel(&b, pixel_coord, 0)),
         1);

      nir_variable *punchthrough =
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_bool_type(), "punchthrough");
      nir_ssa_def *punchthrough_init =
         nir_iand(&b, alpha_bits_1,
                  nir_ieq(&b, nir_iand(&b, color_y, nir_imm_int(&b, 2)), nir_imm_int(&b, 0)));
      nir_store_var(&b, punchthrough, punchthrough_init, 0x1);

      nir_variable *etc1_compat =
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_bool_type(), "etc1_compat");
      nir_store_var(&b, etc1_compat, nir_imm_bool(&b, false), 0x1);

      nir_variable *alpha_result =
         nir_variable_create(b.shader, nir_var_shader_temp, glsl_float_type(), "alpha_result");
      nir_push_if(&b, alpha_bits_8);
      {
         nir_store_var(
            &b, alpha_result,
            decode_etc2_alpha(&b, nir_channels(&b, payload, 3), linear_pixel, false, NULL), 1);
      }
      nir_push_else(&b, NULL);
      {
         nir_store_var(&b, alpha_result, nir_imm_float(&b, 1.0), 1);
      }
      nir_pop_if(&b, NULL);

      const struct glsl_type *uvec3_type = glsl_vector_type(GLSL_TYPE_UINT, 3);
      nir_variable *rgb_result =
         nir_variable_create(b.shader, nir_var_shader_temp, uvec3_type, "rgb_result");
      nir_variable *base_rgb =
         nir_variable_create(b.shader, nir_var_shader_temp, uvec3_type, "base_rgb");
      nir_store_var(&b, rgb_result, nir_imm_ivec3(&b, 255, 0, 0), 0x7);

      nir_ssa_def *msb =
         nir_iand(&b, nir_ushr(&b, color_x, nir_iadd(&b, nir_imm_int(&b, 15), linear_pixel)),
                  nir_imm_int(&b, 2));
      nir_ssa_def *lsb = nir_iand(&b, nir_ushr(&b, color_x, linear_pixel), nir_imm_int(&b, 1));

      nir_push_if(
         &b, nir_iand(&b, nir_inot(&b, alpha_bits_1),
                      nir_ieq(&b, nir_iand(&b, color_y, nir_imm_int(&b, 2)), nir_imm_int(&b, 0))));
      {
         nir_store_var(&b, etc1_compat, nir_imm_bool(&b, true), 1);
         nir_ssa_def *tmp[3];
         for (unsigned i = 0; i < 3; ++i)
            tmp[i] =
               etc_extend(&b,
                          nir_iand(&b,
                                   nir_ushr(&b, color_y,
                                            nir_isub(&b, nir_imm_int(&b, 28 - 8 * i),
                                                     nir_imul(&b, subblock, nir_imm_int(&b, 4)))),
                                   nir_imm_int(&b, 0xf)),
                          4);
         nir_store_var(&b, base_rgb, nir_vec(&b, tmp, 3), 0x7);
      }
      nir_push_else(&b, NULL);
      {
         nir_ssa_def *rb = nir_ubfe(&b, color_y, nir_imm_int(&b, 27), nir_imm_int(&b, 5));
         nir_ssa_def *rd = nir_ibfe(&b, color_y, nir_imm_int(&b, 24), nir_imm_int(&b, 3));
         nir_ssa_def *gb = nir_ubfe(&b, color_y, nir_imm_int(&b, 19), nir_imm_int(&b, 5));
         nir_ssa_def *gd = nir_ibfe(&b, color_y, nir_imm_int(&b, 16), nir_imm_int(&b, 3));
         nir_ssa_def *bb = nir_ubfe(&b, color_y, nir_imm_int(&b, 11), nir_imm_int(&b, 5));
         nir_ssa_def *bd = nir_ibfe(&b, color_y, nir_imm_int(&b, 8), nir_imm_int(&b, 3));
         nir_ssa_def *r1 = nir_iadd(&b, rb, rd);
         nir_ssa_def *g1 = nir_iadd(&b, gb, gd);
         nir_ssa_def *b1 = nir_iadd(&b, bb, bd);

         nir_push_if(&b, nir_ult(&b, nir_imm_int(&b, 31), r1));
         {
            nir_ssa_def *r0 =
               nir_ior(&b, nir_ubfe(&b, color_y, nir_imm_int(&b, 24), nir_imm_int(&b, 2)),
                       nir_ishl(&b, nir_ubfe(&b, color_y, nir_imm_int(&b, 27), nir_imm_int(&b, 2)),
                                nir_imm_int(&b, 2)));
            nir_ssa_def *g0 = nir_ubfe(&b, color_y, nir_imm_int(&b, 20), nir_imm_int(&b, 4));
            nir_ssa_def *b0 = nir_ubfe(&b, color_y, nir_imm_int(&b, 16), nir_imm_int(&b, 4));
            nir_ssa_def *r2 = nir_ubfe(&b, color_y, nir_imm_int(&b, 12), nir_imm_int(&b, 4));
            nir_ssa_def *g2 = nir_ubfe(&b, color_y, nir_imm_int(&b, 8), nir_imm_int(&b, 4));
            nir_ssa_def *b2 = nir_ubfe(&b, color_y, nir_imm_int(&b, 4), nir_imm_int(&b, 4));
            nir_ssa_def *da =
               nir_ior(&b,
                       nir_ishl(&b, nir_ubfe(&b, color_y, nir_imm_int(&b, 2), nir_imm_int(&b, 2)),
                                nir_imm_int(&b, 1)),
                       nir_iand(&b, color_y, nir_imm_int(&b, 1)));
            nir_ssa_def *dist = etc2_distance_lookup(&b, da);
            nir_ssa_def *index = nir_ior(&b, lsb, msb);

            nir_store_var(&b, punchthrough,
                          nir_iand(&b, nir_load_var(&b, punchthrough),
                                   nir_ieq(&b, nir_iadd(&b, lsb, msb), nir_imm_int(&b, 2))),
                          0x1);
            nir_push_if(&b, nir_ieq(&b, index, nir_imm_int(&b, 0)));
            {
               nir_store_var(&b, rgb_result, etc_extend(&b, nir_vec3(&b, r0, g0, b0), 4), 0x7);
            }
            nir_push_else(&b, NULL);
            {

               nir_ssa_def *tmp =
                  nir_iadd(&b, etc_extend(&b, nir_vec3(&b, r2, g2, b2), 4),
                           nir_imul(&b, dist, nir_isub(&b, nir_imm_int(&b, 2), index)));
               nir_store_var(&b, rgb_result, tmp, 0x7);
            }
            nir_pop_if(&b, NULL);
         }
         nir_push_else(&b, NULL);
         nir_push_if(&b, nir_ult(&b, nir_imm_int(&b, 31), g1));
         {
            nir_ssa_def *r0 = nir_ubfe(&b, color_y, nir_imm_int(&b, 27), nir_imm_int(&b, 4));
            nir_ssa_def *g0 = nir_ior(
               &b,
               nir_ishl(&b, nir_ubfe(&b, color_y, nir_imm_int(&b, 24), nir_imm_int(&b, 3)),
                        nir_imm_int(&b, 1)),
               nir_iand(&b, nir_ushr(&b, color_y, nir_imm_int(&b, 20)), nir_imm_int(&b, 1)));
            nir_ssa_def *b0 = nir_ior(
               &b, nir_ubfe(&b, color_y, nir_imm_int(&b, 15), nir_imm_int(&b, 3)),
               nir_iand(&b, nir_ushr(&b, color_y, nir_imm_int(&b, 16)), nir_imm_int(&b, 8)));
            nir_ssa_def *r2 = nir_ubfe(&b, color_y, nir_imm_int(&b, 11), nir_imm_int(&b, 4));
            nir_ssa_def *g2 = nir_ubfe(&b, color_y, nir_imm_int(&b, 7), nir_imm_int(&b, 4));
            nir_ssa_def *b2 = nir_ubfe(&b, color_y, nir_imm_int(&b, 3), nir_imm_int(&b, 4));
            nir_ssa_def *da = nir_iand(&b, color_y, nir_imm_int(&b, 4));
            nir_ssa_def *db = nir_iand(&b, color_y, nir_imm_int(&b, 1));
            nir_ssa_def *d = nir_iadd(&b, da, nir_imul(&b, db, nir_imm_int(&b, 2)));
            nir_ssa_def *d0 = nir_iadd(&b, nir_ishl(&b, r0, nir_imm_int(&b, 16)),
                                       nir_iadd(&b, nir_ishl(&b, g0, nir_imm_int(&b, 8)), b0));
            nir_ssa_def *d2 = nir_iadd(&b, nir_ishl(&b, r2, nir_imm_int(&b, 16)),
                                       nir_iadd(&b, nir_ishl(&b, g2, nir_imm_int(&b, 8)), b2));
            d = nir_bcsel(&b, nir_uge(&b, d0, d2), nir_iadd(&b, d, nir_imm_int(&b, 1)), d);
            nir_ssa_def *dist = etc2_distance_lookup(&b, d);
            nir_ssa_def *base = nir_bcsel(&b, nir_ine(&b, msb, nir_imm_int(&b, 0)),
                                          nir_vec3(&b, r2, g2, b2), nir_vec3(&b, r0, g0, b0));
            base = etc_extend(&b, base, 4);
            base = nir_iadd(
               &b, base,
               nir_imul(&b, dist,
                        nir_isub(&b, nir_imm_int(&b, 1), nir_imul(&b, lsb, nir_imm_int(&b, 2)))));
            nir_store_var(&b, rgb_result, base, 0x7);
            nir_store_var(&b, punchthrough,
                          nir_iand(&b, nir_load_var(&b, punchthrough),
                                   nir_ieq(&b, nir_iadd(&b, lsb, msb), nir_imm_int(&b, 2))),
                          0x1);
         }
         nir_push_else(&b, NULL);
         nir_push_if(&b, nir_ult(&b, nir_imm_int(&b, 31), b1));
         {
            nir_ssa_def *r0 = nir_ubfe(&b, color_y, nir_imm_int(&b, 25), nir_imm_int(&b, 6));
            nir_ssa_def *g0 = nir_ior(
               &b, nir_ubfe(&b, color_y, nir_imm_int(&b, 17), nir_imm_int(&b, 6)),
               nir_iand(&b, nir_ushr(&b, color_y, nir_imm_int(&b, 18)), nir_imm_int(&b, 0x40)));
            nir_ssa_def *b0 = nir_ior(
               &b,
               nir_ishl(&b, nir_ubfe(&b, color_y, nir_imm_int(&b, 11), nir_imm_int(&b, 2)),
                        nir_imm_int(&b, 3)),
               nir_ior(
                  &b,
                  nir_iand(&b, nir_ushr(&b, color_y, nir_imm_int(&b, 11)), nir_imm_int(&b, 0x20)),
                  nir_ubfe(&b, color_y, nir_imm_int(&b, 7), nir_imm_int(&b, 3))));
            nir_ssa_def *rh =
               nir_ior(&b, nir_iand(&b, color_y, nir_imm_int(&b, 1)),
                       nir_ishl(&b, nir_ubfe(&b, color_y, nir_imm_int(&b, 2), nir_imm_int(&b, 5)),
                                nir_imm_int(&b, 1)));
            nir_ssa_def *rv = nir_ubfe(&b, color_x, nir_imm_int(&b, 13), nir_imm_int(&b, 6));
            nir_ssa_def *gh = nir_ubfe(&b, color_x, nir_imm_int(&b, 25), nir_imm_int(&b, 7));
            nir_ssa_def *gv = nir_ubfe(&b, color_x, nir_imm_int(&b, 6), nir_imm_int(&b, 7));
            nir_ssa_def *bh = nir_ubfe(&b, color_x, nir_imm_int(&b, 19), nir_imm_int(&b, 6));
            nir_ssa_def *bv = nir_ubfe(&b, color_x, nir_imm_int(&b, 0), nir_imm_int(&b, 6));

            r0 = etc_extend(&b, r0, 6);
            g0 = etc_extend(&b, g0, 7);
            b0 = etc_extend(&b, b0, 6);
            rh = etc_extend(&b, rh, 6);
            rv = etc_extend(&b, rv, 6);
            gh = etc_extend(&b, gh, 7);
            gv = etc_extend(&b, gv, 7);
            bh = etc_extend(&b, bh, 6);
            bv = etc_extend(&b, bv, 6);

            nir_ssa_def *rgb = nir_vec3(&b, r0, g0, b0);
            nir_ssa_def *dx = nir_imul(&b, nir_isub(&b, nir_vec3(&b, rh, gh, bh), rgb),
                                       nir_channel(&b, pixel_coord, 0));
            nir_ssa_def *dy = nir_imul(&b, nir_isub(&b, nir_vec3(&b, rv, gv, bv), rgb),
                                       nir_channel(&b, pixel_coord, 1));
            rgb = nir_iadd(&b, rgb,
                           nir_ishr(&b, nir_iadd(&b, nir_iadd(&b, dx, dy), nir_imm_int(&b, 2)),
                                    nir_imm_int(&b, 2)));
            nir_store_var(&b, rgb_result, rgb, 0x7);
            nir_store_var(&b, punchthrough, nir_imm_bool(&b, false), 0x1);
         }
         nir_push_else(&b, NULL);
         {
            nir_store_var(&b, etc1_compat, nir_imm_bool(&b, true), 1);
            nir_ssa_def *subblock_b = nir_ine(&b, subblock, nir_imm_int(&b, 0));
            nir_ssa_def *tmp[] = {
               nir_bcsel(&b, subblock_b, r1, rb),
               nir_bcsel(&b, subblock_b, g1, gb),
               nir_bcsel(&b, subblock_b, b1, bb),
            };
            nir_store_var(&b, base_rgb, etc_extend(&b, nir_vec(&b, tmp, 3), 5), 0x7);
         }
         nir_pop_if(&b, NULL);
         nir_pop_if(&b, NULL);
         nir_pop_if(&b, NULL);
      }
      nir_pop_if(&b, NULL);
      nir_push_if(&b, nir_load_var(&b, etc1_compat));
      {
         nir_ssa_def *etc1_table_index =
            nir_ubfe(&b, color_y,
                     nir_isub(&b, nir_imm_int(&b, 5), nir_imul(&b, nir_imm_int(&b, 3), subblock)),
                     nir_imm_int(&b, 3));
         nir_ssa_def *sgn = nir_isub(&b, nir_imm_int(&b, 1), msb);
         sgn = nir_bcsel(&b, nir_load_var(&b, punchthrough), nir_imul(&b, sgn, lsb), sgn);
         nir_store_var(&b, punchthrough,
                       nir_iand(&b, nir_load_var(&b, punchthrough),
                                nir_ieq(&b, nir_iadd(&b, lsb, msb), nir_imm_int(&b, 2))),
                       0x1);
         nir_ssa_def *off =
            nir_imul(&b, etc1_color_modifier_lookup(&b, etc1_table_index, lsb), sgn);
         nir_ssa_def *result = nir_iadd(&b, nir_load_var(&b, base_rgb), off);
         nir_store_var(&b, rgb_result, result, 0x7);
      }
      nir_pop_if(&b, NULL);
      nir_push_if(&b, nir_load_var(&b, punchthrough));
      {
         nir_store_var(&b, alpha_result, nir_imm_float(&b, 0), 0x1);
         nir_store_var(&b, rgb_result, nir_imm_ivec3(&b, 0, 0, 0), 0x7);
      }
      nir_pop_if(&b, NULL);
      nir_ssa_def *col[4];
      for (unsigned i = 0; i < 3; ++i)
         col[i] = nir_fdiv(&b, nir_i2f32(&b, nir_channel(&b, nir_load_var(&b, rgb_result), i)),
                           nir_imm_float(&b, 255.0));
      col[3] = nir_load_var(&b, alpha_result);
      nir_store_var(&b, color, nir_vec(&b, col, 4), 0xf);
   }
   nir_push_else(&b, NULL);
   { /* EAC */
      nir_ssa_def *is_signed =
         nir_ior(&b, nir_ieq(&b, format, nir_imm_int(&b, VK_FORMAT_EAC_R11_SNORM_BLOCK)),
                 nir_ieq(&b, format, nir_imm_int(&b, VK_FORMAT_EAC_R11G11_SNORM_BLOCK)));
      nir_ssa_def *val[4];
      for (int i = 0; i < 2; ++i) {
         val[i] = decode_etc2_alpha(&b, nir_channels(&b, payload, 3 << (2 * i)), linear_pixel, true,
                                    is_signed);
      }
      val[2] = nir_imm_float(&b, 0.0);
      val[3] = nir_imm_float(&b, 1.0);
      nir_store_var(&b, color, nir_vec(&b, val, 4), 0xf);
   }
   nir_pop_if(&b, NULL);

   nir_ssa_def *outval = nir_load_var(&b, color);
   nir_ssa_def *img_coord = nir_vec4(&b, nir_channel(&b, coord, 0), nir_channel(&b, coord, 1),
                                     nir_channel(&b, coord, 2), nir_ssa_undef(&b, 1, 32));

   nir_push_if(&b, is_3d);
   {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img_3d)->dest.ssa, img_coord,
                            nir_ssa_undef(&b, 1, 32), outval, nir_imm_int(&b, 0),
                            .image_dim = GLSL_SAMPLER_DIM_3D);
   }
   nir_push_else(&b, NULL);
   {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img_2d)->dest.ssa, img_coord,
                            nir_ssa_undef(&b, 1, 32), outval, nir_imm_int(&b, 0),
                            .image_dim = GLSL_SAMPLER_DIM_2D, .image_array = true);
   }
   nir_pop_if(&b, NULL);
   return b.shader;
}

static VkResult
create_layout(struct radv_device *device)
{
   VkResult result;
   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.etc_decode.ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.etc_decode.ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 20},
   };

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.etc_decode.p_layout);
   if (result != VK_SUCCESS)
      goto fail;
   return VK_SUCCESS;
fail:
   return result;
}

static VkResult
create_decode_pipeline(struct radv_device *device, VkPipeline *pipeline)
{
   VkResult result;

   mtx_lock(&device->meta_state.mtx);
   if (*pipeline) {
      mtx_unlock(&device->meta_state.mtx);
      return VK_SUCCESS;
   }

   nir_shader *cs = build_shader(device);

   /* compute shader */

   VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = pipeline_shader_stage,
      .flags = 0,
      .layout = device->meta_state.resolve_compute.p_layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &vk_pipeline_info, NULL, pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   ralloc_free(cs);
   mtx_unlock(&device->meta_state.mtx);
   return VK_SUCCESS;
fail:
   ralloc_free(cs);
   mtx_unlock(&device->meta_state.mtx);
   return result;
}

VkResult
radv_device_init_meta_etc_decode_state(struct radv_device *device, bool on_demand)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult res;

   if (!device->physical_device->emulate_etc2)
      return VK_SUCCESS;

   res = create_layout(device);
   if (res != VK_SUCCESS)
      goto fail;

   if (on_demand)
      return VK_SUCCESS;

   res = create_decode_pipeline(device, &state->etc_decode.pipeline);
   if (res != VK_SUCCESS)
      goto fail;

   return VK_SUCCESS;
fail:
   radv_device_finish_meta_etc_decode_state(device);
   return res;
}

void
radv_device_finish_meta_etc_decode_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;
   radv_DestroyPipeline(radv_device_to_handle(device), state->etc_decode.pipeline, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->etc_decode.p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->etc_decode.ds_layout,
                                   &state->alloc);
}

static VkPipeline
radv_get_etc_decode_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
   struct radv_device *device = cmd_buffer->device;
   VkPipeline *pipeline = &device->meta_state.etc_decode.pipeline;

   if (!*pipeline) {
      VkResult ret;

      ret = create_decode_pipeline(device, pipeline);
      if (ret != VK_SUCCESS) {
         cmd_buffer->record_result = ret;
         return VK_NULL_HANDLE;
      }
   }

   return *pipeline;
}

static void
decode_etc(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview,
           struct radv_image_view *dest_iview, const VkOffset3D *offset, const VkExtent3D *extent)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.resolve_compute.p_layout,
      0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = 0,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                .pImageInfo =
                                   (VkDescriptorImageInfo[]){
                                      {.sampler = VK_NULL_HANDLE,
                                       .imageView = radv_image_view_to_handle(src_iview),
                                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                   }},
                               {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = 1,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                .pImageInfo = (VkDescriptorImageInfo[]){
                                   {
                                      .sampler = VK_NULL_HANDLE,
                                      .imageView = radv_image_view_to_handle(dest_iview),
                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   },
                                }}});

   VkPipeline pipeline = radv_get_etc_decode_pipeline(cmd_buffer);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   unsigned push_constants[5] = {
      offset->x, offset->y, offset->z, src_iview->image->vk_format, src_iview->image->type,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         device->meta_state.resolve_compute.p_layout, VK_SHADER_STAGE_COMPUTE_BIT,
                         0, 20, push_constants);
   radv_unaligned_dispatch(cmd_buffer, extent->width, extent->height, extent->depth);
}

void
radv_meta_decode_etc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                     VkImageLayout layout, const VkImageSubresourceLayers *subresource,
                     VkOffset3D offset, VkExtent3D extent)
{
   struct radv_meta_saved_state saved_state;
   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS);

   bool old_predicating = cmd_buffer->state.predicating;
   cmd_buffer->state.predicating = false;

   uint32_t base_slice = radv_meta_get_iview_layer(image, subresource, &offset);
   uint32_t slice_count = image->type == VK_IMAGE_TYPE_3D ? extent.depth : subresource->layerCount;

   extent = radv_sanitize_image_extent(image->type, extent);
   offset = radv_sanitize_image_offset(image->type, offset);

   VkFormat load_format = vk_format_get_blocksize(image->vk_format) == 16
                             ? VK_FORMAT_R32G32B32A32_UINT
                             : VK_FORMAT_R32G32_UINT;
   struct radv_image_view src_iview;
   radv_image_view_init(
      &src_iview, cmd_buffer->device,
      &(VkImageViewCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = radv_image_to_handle(image),
         .viewType = radv_meta_get_view_type(image),
         .format = load_format,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT,
               .baseMipLevel = subresource->mipLevel,
               .levelCount = 1,
               .baseArrayLayer = 0,
               .layerCount = subresource->baseArrayLayer + subresource->layerCount,
            },
      },
      NULL);

   VkFormat store_format;
   switch (image->vk_format) {
   case VK_FORMAT_EAC_R11_UNORM_BLOCK:
      store_format = VK_FORMAT_R16_UNORM;
      break;
   case VK_FORMAT_EAC_R11_SNORM_BLOCK:
      store_format = VK_FORMAT_R16_SNORM;
      break;
   case VK_FORMAT_EAC_R11G11_UNORM_BLOCK:
      store_format = VK_FORMAT_R16G16_UNORM;
      break;
   case VK_FORMAT_EAC_R11G11_SNORM_BLOCK:
      store_format = VK_FORMAT_R16G16_SNORM;
      break;
   default:
      store_format = VK_FORMAT_R8G8B8A8_UNORM;
   }
   struct radv_image_view dest_iview;
   radv_image_view_init(
      &dest_iview, cmd_buffer->device,
      &(VkImageViewCreateInfo){
         .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
         .image = radv_image_to_handle(image),
         .viewType = radv_meta_get_view_type(image),
         .format = store_format,
         .subresourceRange =
            {
               .aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT,
               .baseMipLevel = subresource->mipLevel,
               .levelCount = 1,
               .baseArrayLayer = 0,
               .layerCount = subresource->baseArrayLayer + subresource->layerCount,
            },
      },
      NULL);

   decode_etc(cmd_buffer, &src_iview, &dest_iview, &(VkOffset3D){offset.x, offset.y, base_slice},
              &(VkExtent3D){extent.width, extent.height, slice_count});

   radv_image_view_finish(&src_iview);
   radv_image_view_finish(&dest_iview);

   cmd_buffer->state.predicating = old_predicating;
   radv_meta_restore(&saved_state, cmd_buffer);
}
