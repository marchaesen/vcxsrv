/*
 * Copyright © 2020 Mike Blumenkrantz
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
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "nir.h"
#include "nir_builder.h"

/**
 * This pass uses the enabled clip planes from the rasterizer state to rewrite
 * vertex shader store operations and store a 0 to the corresponding gl_ClipDistance[n]
 * value if the plane is disabled
 */

/* vulkan (and some drivers) provides no concept of enabling clip planes through api,
 * so we rewrite disabled clip planes to a zero value in order to disable them
 */
static bool
lower_clip_plane_store_io(nir_builder *b, nir_intrinsic_instr *intr,
                          void *cb_data)
{
   unsigned clip_plane_enable = *(unsigned *)cb_data;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
   case nir_intrinsic_store_per_primitive_output:
   case nir_intrinsic_store_per_vertex_output:
   case nir_intrinsic_store_per_view_output:
      break;
   default:
      return false;
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location != VARYING_SLOT_CLIP_DIST0 &&
       sem.location != VARYING_SLOT_CLIP_DIST1)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_src *src_offset = nir_get_io_offset_src(intr);
   unsigned wrmask = nir_intrinsic_write_mask(intr);
   unsigned base_index = (sem.location == VARYING_SLOT_CLIP_DIST1 ? 4 : 0) +
                         nir_intrinsic_component(intr);
   nir_def *zero = nir_imm_int(b, 0);

   if (nir_src_is_const(*src_offset)) {
      base_index += nir_src_as_uint(*src_offset) * 4;

      u_foreach_bit(bit, wrmask) {
         if (!(clip_plane_enable & BITFIELD_BIT(base_index + bit))) {
            nir_def *vec = nir_vector_insert_imm(b, intr->src[0].ssa, zero, bit);
            nir_src_rewrite(&intr->src[0], vec);
         }
      }
   } else {
      u_foreach_bit(bit, wrmask) {
         unsigned index = base_index + bit;
         nir_def *chan = nir_channel(b, intr->src[0].ssa, bit);
         nir_def *dist0 = clip_plane_enable & BITFIELD_BIT(index) ? chan : zero;
         nir_def *dist1 = clip_plane_enable & BITFIELD_BIT(index + 4) ? chan : zero;
         chan = nir_bcsel(b, nir_ieq_imm(b, src_offset->ssa, 0), dist0, dist1);
         nir_def *vec = nir_vector_insert_imm(b, intr->src[0].ssa, chan, bit);
         nir_src_rewrite(&intr->src[0], vec);
      }
   }
   return true;
}

bool
nir_lower_clip_disable(nir_shader *shader, unsigned clip_plane_enable)
{
   assert(shader->info.io_lowered);

   /* if all user planes are enabled in API that are written in the array, always ignore;
    * this explicitly covers the 2x vec4 case
    */
   if (clip_plane_enable == u_bit_consecutive(0, shader->info.clip_distance_array_size))
      return false;

   return nir_shader_intrinsics_pass(shader, lower_clip_plane_store_io,
                                     nir_metadata_control_flow,
                                     &clip_plane_enable);
}
