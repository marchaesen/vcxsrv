/*
 * Copyright (C) 2024 Collabora, Ltd.
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
 *
 * Authors (Collabora):
 *   Eric Smith <eric.smith@collabora.com>
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "pan_ir.h"

static bool
nir_lower_image_ms(nir_builder *b, nir_intrinsic_instr *intr,
                        UNUSED void *data)
{
   bool img_deref = false;

   switch (intr->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
      img_deref = true;
      break;
   case nir_intrinsic_image_texel_address:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
      break;
   default:
      return false;
   }

   if (nir_intrinsic_image_dim(intr) != GLSL_SAMPLER_DIM_MS)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *coord = intr->src[1].ssa;
   nir_def *sample = nir_channel(b, intr->src[2].ssa, 0);

   if (nir_intrinsic_image_array(intr)) {
      /* Unlike textures, images only embed a single LOD, hence the zero. */
      nir_def *lod = nir_imm_int(b, 0);
      nir_def *img_size =
         img_deref ? nir_image_deref_size(b, 3, 32, intr->src[0].ssa, lod) :
         nir_image_size(b, 3, 32, intr->src[0].ssa, lod,
                        .image_array = true, .image_dim = GLSL_SAMPLER_DIM_MS);
      nir_def *img_height = nir_channel(b, img_size, 1);
      nir_def *y_coord = nir_channel(b, coord, 1);
      nir_def *z_coord = nir_channel(b, coord, 2);

      /* With image2DMSArray, the Z coord is already used to index the array. We
       * assume sample planes are adjacent and patch the Y coordinate to address
       * the right sample plane. This means our image height is effectively
       * limited to 4k though.
       *
       * Note that we don't trust image intrinsic is_array information because
       * arrays of size one are allowed, and we only get to know the actual
       * image size at bind time.
       */
      nir_def *is_array = nir_ugt_imm(b, nir_channel(b, img_size, 2), 1);

      y_coord = nir_bcsel(
         b, is_array,
         nir_iadd(b, nir_imul(b, img_height, sample), y_coord), y_coord);
      z_coord = nir_bcsel(b, is_array, z_coord, sample);
      coord = nir_vec4(b, nir_channel(b, coord, 0), y_coord, z_coord,
                       nir_channel(b, coord, 3));

      nir_src_rewrite(&intr->src[1], coord);
   } else {
      /* image2DMS is treated by panfrost as if it were a 3D image, so
       * the sample index is in src[2]. We need to put this into the coordinates
       * in the Z component.
       */
      nir_src_rewrite(&intr->src[1],
                      nir_vector_insert_imm(b, coord, sample, 2));
   }

   nir_intrinsic_set_image_dim(intr, GLSL_SAMPLER_DIM_3D);
   nir_intrinsic_set_image_array(intr, false);
   return true;
}

bool
pan_nir_lower_image_ms(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(
      shader, nir_lower_image_ms,
      nir_metadata_control_flow, NULL);
}
