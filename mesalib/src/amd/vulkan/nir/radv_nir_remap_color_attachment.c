/*
 * Copyright © 2024 Valve Corporation
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

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "radv_constants.h"
#include "radv_nir.h"
#include "radv_pipeline_graphics.h"
#include "vk_graphics_state.h"

static bool
remap_color_attachment(nir_builder *b, nir_intrinsic_instr *intrin, void *state)
{
   const uint8_t *color_remap = (uint8_t *)state;

   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   if (io_sem.location < FRAG_RESULT_DATA0)
      return false;

   if (io_sem.dual_source_blend_index)
      return false;

   const unsigned location = io_sem.location - FRAG_RESULT_DATA0;
   if (color_remap[location] == MESA_VK_ATTACHMENT_UNUSED) {
      nir_instr_remove(&intrin->instr);
      return false;
   }

   const unsigned new_location = FRAG_RESULT_DATA0 + color_remap[location];

   io_sem.location = new_location;

   nir_intrinsic_set_io_semantics(intrin, io_sem);

   return true;
}

bool
radv_nir_remap_color_attachment(nir_shader *shader, const struct radv_graphics_state_key *gfx_state)
{
   uint8_t color_remap[MAX_RTS];

   /* Shader output locations to color attachment mappings. */
   memset(color_remap, MESA_VK_ATTACHMENT_UNUSED, sizeof(color_remap));
   for (uint32_t i = 0; i < MAX_RTS; i++) {
      if (gfx_state->ps.epilog.color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
         color_remap[gfx_state->ps.epilog.color_map[i]] = i;
   }

   return nir_shader_intrinsics_pass(shader, remap_color_attachment, nir_metadata_all, &color_remap);
}
