/*
 * Copyright (c) 2022 Amazon.com, Inc. or its affiliates.
 * Copyright (C) 2019-2022 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "pan_ir.h"

static enum pipe_format
varying_format(nir_alu_type t, unsigned ncomps)
{
   assert(ncomps >= 1 && ncomps <= 4);

#define VARYING_FORMAT(ntype, nsz, ptype, psz)                                 \
   {                                                                           \
      .type = nir_type_##ntype##nsz, .formats = {                              \
         PIPE_FORMAT_R##psz##_##ptype,                                         \
         PIPE_FORMAT_R##psz##G##psz##_##ptype,                                 \
         PIPE_FORMAT_R##psz##G##psz##B##psz##_##ptype,                         \
         PIPE_FORMAT_R##psz##G##psz##B##psz##A##psz##_##ptype,                 \
      }                                                                        \
   }

   static const struct {
      nir_alu_type type;
      enum pipe_format formats[4];
   } conv[] = {
      VARYING_FORMAT(float, 32, FLOAT, 32),
      VARYING_FORMAT(uint, 32, UINT, 32),
      VARYING_FORMAT(float, 16, FLOAT, 16),
      VARYING_FORMAT(uint, 16, UINT, 16),
   };
#undef VARYING_FORMAT

   assert(ncomps > 0 && ncomps <= ARRAY_SIZE(conv[0].formats));

   for (unsigned i = 0; i < ARRAY_SIZE(conv); i++) {
      if (conv[i].type == t)
         return conv[i].formats[ncomps - 1];
   }

   unreachable("Invalid type");
}

struct slot_info {
   nir_alu_type type;
   unsigned count;
   unsigned index;
};

struct walk_varyings_data {
   enum pan_mediump_vary mediump;
   struct pan_shader_info *info;
   struct slot_info *slots;
};

static bool
walk_varyings(UNUSED nir_builder *b, nir_instr *instr, void *data)
{
   struct walk_varyings_data *wv_data = data;
   struct pan_shader_info *info = wv_data->info;
   struct slot_info *slots = wv_data->slots;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
   unsigned count;
   unsigned size;

   /* Only consider intrinsics that access varyings */
   switch (intr->intrinsic) {
   case nir_intrinsic_store_output:
      if (b->shader->info.stage != MESA_SHADER_VERTEX)
         return false;

      count = nir_src_num_components(intr->src[0]);
      size = nir_alu_type_get_type_size(nir_intrinsic_src_type(intr));
      break;

   case nir_intrinsic_load_input:
   case nir_intrinsic_load_interpolated_input:
      if (b->shader->info.stage != MESA_SHADER_FRAGMENT)
         return false;

      count = intr->def.num_components;
      size = intr->def.bit_size;
      break;

   default:
      return false;
   }

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);

   if (sem.no_varying)
      return false;

   /* In a fragment shader, flat shading is lowered to load_input but
    * interpolation is lowered to load_interpolated_input, so we can check
    * the intrinsic to distinguish.
    *
    * In a vertex shader, we consider everything flat, as the information
    * will not contribute to the final linked varyings -- flatness is used
    * only to determine the type, and the GL linker uses the type from the
    * fragment shader instead.
    */
   bool flat = intr->intrinsic != nir_intrinsic_load_interpolated_input;
   bool auto32 = !info->quirk_no_auto32 && size == 32;
   nir_alu_type type = (flat && auto32) ? nir_type_uint : nir_type_float;

   if (sem.medium_precision) {
      /* Demote interpolated float varyings to fp16 where possible. We do not
       * demote flat varyings, including integer varyings, due to various
       * issues with the Midgard hardware behaviour and TGSI shaders, as well
       * as having no demonstrable benefit in practice.
       */
      if (wv_data->mediump == PAN_MEDIUMP_VARY_SMOOTH_16BIT)
         size = type == nir_type_float ? 16 : 32;

      if (wv_data->mediump == PAN_MEDIUMP_VARY_32BIT)
         size = 32;
   }

   assert(size == 32 || size == 16);
   type |= size;

   /* Count currently contains the number of components accessed by this
    * intrinsics. However, we may be accessing a fractional location,
    * indicating by the NIR component. Add that in. The final value be the
    * maximum (component + count), an upper bound on the number of
    * components possibly used.
    */
   count += nir_intrinsic_component(intr);

   /* Consider each slot separately */
   for (unsigned offset = 0; offset < sem.num_slots; ++offset) {
      unsigned location = sem.location + offset;
      unsigned index = pan_res_handle_get_index(nir_intrinsic_base(intr)) + offset;

      if (slots[location].type) {
         assert(slots[location].type == type);
         assert(slots[location].index == index);
      } else {
         slots[location].type = type;
         slots[location].index = index;
      }

      slots[location].count = MAX2(slots[location].count, count);
   }

   return false;
}

static bool
collect_noperspective_varyings_fs(UNUSED nir_builder *b,
                                  nir_intrinsic_instr *intr,
                                  void *data)
{
   uint32_t *noperspective_varyings = data;

   if (intr->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   if (sem.location < VARYING_SLOT_VAR0)
      return false;

   nir_intrinsic_instr *bary_instr = nir_src_as_intrinsic(intr->src[0]);
   assert(bary_instr);
   if (nir_intrinsic_interp_mode(bary_instr) == INTERP_MODE_NOPERSPECTIVE)
      *noperspective_varyings |= BITFIELD_BIT(sem.location - VARYING_SLOT_VAR0);

   return false;
}

uint32_t
pan_nir_collect_noperspective_varyings_fs(nir_shader *s)
{
   assert(s->info.stage == MESA_SHADER_FRAGMENT);

   uint32_t noperspective_varyings = 0;
   nir_shader_intrinsics_pass(s, collect_noperspective_varyings_fs,
                              nir_metadata_all,
                              (void *)&noperspective_varyings);

   return noperspective_varyings;
}

void
pan_nir_collect_varyings(nir_shader *s, struct pan_shader_info *info,
                         enum pan_mediump_vary mediump)
{
   if (s->info.stage != MESA_SHADER_VERTEX &&
       s->info.stage != MESA_SHADER_FRAGMENT)
      return;

   struct slot_info slots[64] = {0};
   struct walk_varyings_data wv_data = {mediump, info, slots};
   nir_shader_instructions_pass(s, walk_varyings, nir_metadata_all, &wv_data);

   struct pan_shader_varying *varyings = (s->info.stage == MESA_SHADER_VERTEX)
                                            ? info->varyings.output
                                            : info->varyings.input;

   unsigned count = 0;

   for (unsigned i = 0; i < ARRAY_SIZE(slots); ++i) {
      if (!slots[i].type)
         continue;

      enum pipe_format format = varying_format(slots[i].type, slots[i].count);
      assert(format != PIPE_FORMAT_NONE);

      unsigned index = slots[i].index;
      count = MAX2(count, index + 1);

      varyings[index].location = i;
      varyings[index].format = format;
   }

   if (s->info.stage == MESA_SHADER_VERTEX)
      info->varyings.output_count = count;
   else
      info->varyings.input_count = count;

   if (s->info.stage == MESA_SHADER_FRAGMENT)
      info->varyings.noperspective =
         pan_nir_collect_noperspective_varyings_fs(s);
}
