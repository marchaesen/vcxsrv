/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_format_convert.h"

#include "pipe/p_state.h"
#include "util/format/u_format.h"

#include "d3d12_compiler.h"
#include "d3d12_nir_passes.h"

static nir_ssa_def *
convert_value(nir_builder *b, nir_ssa_def *value,
   const struct util_format_description *from_desc,
   const struct util_format_description *to_desc)
{
   if (from_desc->format == to_desc->format)
      return value;

   assert(value->num_components == 4);
   /* No support for 16 or 64 bit data in the shader for image loads/stores */
   assert(value->bit_size == 32);
   /* Overall format size needs to be the same */
   assert(from_desc->block.bits == to_desc->block.bits);
   assert(from_desc->nr_channels <= 4 && to_desc->nr_channels <= 4);

   const unsigned rgba1010102_bits[] = { 10, 10, 10, 2 };

   /* First, construct a "tightly packed" vector of the input values. For unorm/snorm, convert
    * from the float we're given into the original bits (only happens while storing). For packed
    * formats that don't fall on a nice bit size, convert/pack them into 32bit values. Otherwise,
    * just produce a vecNx4 where N is the expected bit size.
    */
   nir_ssa_def *src_as_vec;
   if (from_desc->format == PIPE_FORMAT_R10G10B10A2_UINT ||
       from_desc->format == PIPE_FORMAT_R10G10B10A2_UNORM) {
      if (from_desc->format == PIPE_FORMAT_R10G10B10A2_UNORM)
         value = nir_format_float_to_unorm(b, value, rgba1010102_bits);
      nir_ssa_def *channels[4];
      for (unsigned i = 0; i < 4; ++i)
         channels[i] = nir_channel(b, value, i);

      src_as_vec = channels[0];
      src_as_vec = nir_mask_shift_or(b, src_as_vec, channels[1], (1 << 10) - 1, 10);
      src_as_vec = nir_mask_shift_or(b, src_as_vec, channels[2], (1 << 10) - 1, 20);
      src_as_vec = nir_mask_shift_or(b, src_as_vec, channels[3], (1 << 2) - 1, 30);
   } else if (from_desc->format == PIPE_FORMAT_R11G11B10_FLOAT) {
      src_as_vec = nir_format_pack_11f11f10f(b, value);
   } else if (from_desc->is_unorm) {
      if (from_desc->channel[0].size == 8)
         src_as_vec = nir_pack_unorm_4x8(b, value);
      else {
         nir_ssa_def *packed_channels[2];
         packed_channels[0] = nir_pack_unorm_2x16(b, nir_channels(b, value, 0x3));
         packed_channels[1] = nir_pack_unorm_2x16(b, nir_channels(b, value, 0x3 << 2));
         src_as_vec = nir_vec(b, packed_channels, 2);
      }
   } else if (from_desc->is_snorm) {
      if (from_desc->channel[0].size == 8)
         src_as_vec = nir_pack_snorm_4x8(b, value);
      else {
         nir_ssa_def *packed_channels[2];
         packed_channels[0] = nir_pack_snorm_2x16(b, nir_channels(b, value, 0x3));
         packed_channels[1] = nir_pack_snorm_2x16(b, nir_channels(b, value, 0x3 << 2));
         src_as_vec = nir_vec(b, packed_channels, 2);
      }
   } else if (util_format_is_float(from_desc->format)) {
      src_as_vec = nir_f2fN(b, value, from_desc->channel[0].size);
   } else if (util_format_is_pure_sint(from_desc->format)) {
      src_as_vec = nir_i2iN(b, value, from_desc->channel[0].size);
   } else {
      src_as_vec = nir_u2uN(b, value, from_desc->channel[0].size);
   }

   /* Now that we have the tightly packed bits, we can use nir_extract_bits to get it into a
    * vector of differently-sized components. For producing packed formats, get a 32-bit
    * value and manually extract the bits. For unorm/snorm, get one or two 32-bit values,
    * and extract it using helpers. Otherwise, get a format-sized dest vector and use a
    * cast to expand it back to 32-bit.
    * 
    * Pay extra attention for changing semantics for alpha as 1.
    */
   if (to_desc->format == PIPE_FORMAT_R10G10B10A2_UINT ||
       to_desc->format == PIPE_FORMAT_R10G10B10A2_UNORM) {
      nir_ssa_def *u32 = nir_extract_bits(b, &src_as_vec, 1, 0, 1, 32);
      nir_ssa_def *channels[4] = {
         nir_iand(b, u32,                                  nir_imm_int(b, (1 << 10) - 1)),
         nir_iand(b, nir_ushr(b, u32, nir_imm_int(b, 10)), nir_imm_int(b, (1 << 10) - 1)),
         nir_iand(b, nir_ushr(b, u32, nir_imm_int(b, 20)), nir_imm_int(b, (1 << 10) - 1)),
                     nir_ushr(b, u32, nir_imm_int(b, 30))
      };
      nir_ssa_def *vec = nir_vec(b, channels, 4);
      if (to_desc->format == PIPE_FORMAT_R10G10B10A2_UNORM)
         vec = nir_format_unorm_to_float(b, vec, rgba1010102_bits);
      return vec;
   } else if (to_desc->format == PIPE_FORMAT_R11G11B10_FLOAT) {
      nir_ssa_def *u32 = nir_extract_bits(b, &src_as_vec, 1, 0, 1, 32);
      nir_ssa_def *vec3 = nir_format_unpack_11f11f10f(b, u32);
      return nir_vec4(b, nir_channel(b, vec3, 0),
                         nir_channel(b, vec3, 1),
                         nir_channel(b, vec3, 2),
                         nir_imm_float(b, 1.0f));
   } else if (to_desc->is_unorm || to_desc->is_snorm) {
      nir_ssa_def *dest_packed = nir_extract_bits(b, &src_as_vec, 1, 0,
         DIV_ROUND_UP(to_desc->nr_channels * to_desc->channel[0].size, 32), 32);
      if (to_desc->is_unorm) {
         if (to_desc->channel[0].size == 8) {
            nir_ssa_def *unpacked = nir_unpack_unorm_4x8(b, nir_channel(b, dest_packed, 0));
            if (to_desc->nr_channels < 4)
               unpacked = nir_vector_insert_imm(b, unpacked, nir_imm_float(b, 1.0f), 3);
            return unpacked;
         }
         nir_ssa_def *vec2s[2] = {
            nir_unpack_unorm_2x16(b, nir_channel(b, dest_packed, 0)),
            to_desc->nr_channels > 2 ?
               nir_unpack_unorm_2x16(b, nir_channel(b, dest_packed, 1)) :
               nir_vec2(b, nir_imm_float(b, 0.0f), nir_imm_float(b, 1.0f))
         };
         if (to_desc->nr_channels == 1)
            vec2s[0] = nir_vector_insert_imm(b, vec2s[0], nir_imm_float(b, 0.0f), 1);
         return nir_vec4(b, nir_channel(b, vec2s[0], 0),
                            nir_channel(b, vec2s[0], 1),
                            nir_channel(b, vec2s[1], 0),
                            nir_channel(b, vec2s[1], 1));
      } else {
         if (to_desc->channel[0].size == 8) {
            nir_ssa_def *unpacked = nir_unpack_snorm_4x8(b, nir_channel(b, dest_packed, 0));
            if (to_desc->nr_channels < 4)
               unpacked = nir_vector_insert_imm(b, unpacked, nir_imm_float(b, 1.0f), 3);
            return unpacked;
         }
         nir_ssa_def *vec2s[2] = {
            nir_unpack_snorm_2x16(b, nir_channel(b, dest_packed, 0)),
            to_desc->nr_channels > 2 ?
               nir_unpack_snorm_2x16(b, nir_channel(b, dest_packed, 1)) :
               nir_vec2(b, nir_imm_float(b, 0.0f), nir_imm_float(b, 1.0f))
         };
         if (to_desc->nr_channels == 1)
            vec2s[0] = nir_vector_insert_imm(b, vec2s[0], nir_imm_float(b, 0.0f), 1);
         return nir_vec4(b, nir_channel(b, vec2s[0], 0),
                            nir_channel(b, vec2s[0], 1),
                            nir_channel(b, vec2s[1], 0),
                            nir_channel(b, vec2s[1], 1));
      }
   } else {
      nir_ssa_def *dest_packed = nir_extract_bits(b, &src_as_vec, 1, 0,
         to_desc->nr_channels, to_desc->channel[0].size);
      nir_ssa_def *final_channels[4];
      for (unsigned i = 0; i < 4; ++i) {
         if (i >= dest_packed->num_components)
            final_channels[i] = util_format_is_float(to_desc->format) ?
            nir_imm_floatN_t(b, i == 3 ? 1.0f : 0.0f, to_desc->channel[0].size) :
            nir_imm_intN_t(b, i == 3 ? 1 : 0, to_desc->channel[0].size);
         else
            final_channels[i] = nir_channel(b, dest_packed, i);
      }
      nir_ssa_def *final_vec = nir_vec(b, final_channels, 4);
      if (util_format_is_float(to_desc->format))
         return nir_f2f32(b, final_vec);
      else if (util_format_is_pure_sint(to_desc->format))
         return nir_i2i32(b, final_vec);
      else
         return nir_u2u32(b, final_vec);
   }
}

static bool
lower_image_cast_instr(nir_builder *b, nir_instr *instr, void *_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   if (intr->intrinsic != nir_intrinsic_image_deref_load &&
       intr->intrinsic != nir_intrinsic_image_deref_store)
      return false;

   const struct d3d12_image_format_conversion_info *info = _data;
   nir_variable *image = nir_deref_instr_get_variable(nir_src_as_deref(intr->src[0]));
   assert(image);

   enum pipe_format emulation_format = info[image->data.driver_location].emulated_format;
   if (emulation_format == PIPE_FORMAT_NONE)
      return false;

   enum pipe_format real_format = info[image->data.driver_location].view_format;
   assert(real_format != emulation_format);

   nir_ssa_def *value;
   const struct util_format_description *from_desc, *to_desc;
   if (intr->intrinsic == nir_intrinsic_image_deref_load) {
      b->cursor = nir_after_instr(instr);
      value = &intr->dest.ssa;
      from_desc = util_format_description(emulation_format);
      to_desc = util_format_description(real_format);
   } else {
      b->cursor = nir_before_instr(instr);
      value = intr->src[3].ssa;
      from_desc = util_format_description(real_format);
      to_desc = util_format_description(emulation_format);
   }

   nir_ssa_def *new_value = convert_value(b, value, from_desc, to_desc);

   nir_alu_type alu_type = util_format_is_pure_uint(emulation_format) ?
      nir_type_uint : (util_format_is_pure_sint(emulation_format) ?
         nir_type_int : nir_type_float);

   if (intr->intrinsic == nir_intrinsic_image_deref_load) {
      nir_ssa_def_rewrite_uses_after(value, new_value, new_value->parent_instr);
      nir_intrinsic_set_dest_type(intr, alu_type);
   } else {
      nir_instr_rewrite_src_ssa(instr, &intr->src[3], new_value);
      nir_intrinsic_set_src_type(intr, alu_type);
   }
   nir_intrinsic_set_format(intr, emulation_format);
   return true;
}

/* Given a shader that does image loads/stores expecting to load from the format embedded in the intrinsic,
 * if the corresponding entry in formats is not PIPE_FORMAT_NONE, replace the image format and convert
 * the data being loaded/stored to/from the app's expected format.
 */
bool
d3d12_lower_image_casts(nir_shader *s, struct d3d12_image_format_conversion_info *info)
{
   bool progress = nir_shader_instructions_pass(s, lower_image_cast_instr,
      nir_metadata_block_index | nir_metadata_dominance, info);

   if (progress) {
      nir_foreach_image_variable(var, s) {
         if (info[var->data.driver_location].emulated_format != PIPE_FORMAT_NONE) {
            var->data.image.format = info[var->data.driver_location].emulated_format;
         }
      }
   }

   return progress;
}
