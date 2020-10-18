/*
 * Copyright © 2018 Intel Corporation
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

#include "nir_xfb_info.h"

#include <util/u_math.h>

static void
add_var_xfb_varying(nir_xfb_info *xfb,
                    nir_xfb_varyings_info *varyings,
                    unsigned buffer,
                    unsigned offset,
                    const struct glsl_type *type)
{
   if (varyings == NULL)
      return;

   nir_xfb_varying_info *varying = &varyings->varyings[varyings->varying_count++];

   varying->type = type;
   varying->buffer = buffer;
   varying->offset = offset;
   xfb->buffers[buffer].varying_count++;
}


static nir_xfb_info *
nir_xfb_info_create(void *mem_ctx, uint16_t output_count)
{
   return rzalloc_size(mem_ctx, nir_xfb_info_size(output_count));
}

static size_t
nir_xfb_varyings_info_size(uint16_t varying_count)
{
   return sizeof(nir_xfb_info) + sizeof(nir_xfb_varying_info) * varying_count;
}

static nir_xfb_varyings_info *
nir_xfb_varyings_info_create(void *mem_ctx, uint16_t varying_count)
{
   return rzalloc_size(mem_ctx, nir_xfb_varyings_info_size(varying_count));
}

static void
add_var_xfb_outputs(nir_xfb_info *xfb,
                    nir_xfb_varyings_info *varyings,
                    nir_variable *var,
                    unsigned buffer,
                    unsigned *location,
                    unsigned *offset,
                    const struct glsl_type *type,
                    bool varying_added)
{
   /* If this type contains a 64-bit value, align to 8 bytes */
   if (glsl_type_contains_64bit(type))
      *offset = ALIGN_POT(*offset, 8);

   if (glsl_type_is_array_or_matrix(type) && !var->data.compact) {
      unsigned length = glsl_get_length(type);

      const struct glsl_type *child_type = glsl_get_array_element(type);
      if (!glsl_type_is_array(child_type) &&
          !glsl_type_is_struct(child_type)) {

         add_var_xfb_varying(xfb, varyings, buffer, *offset, type);
         varying_added = true;
      }

      for (unsigned i = 0; i < length; i++)
         add_var_xfb_outputs(xfb, varyings, var, buffer, location, offset,
                             child_type, varying_added);
   } else if (glsl_type_is_struct_or_ifc(type)) {
      unsigned length = glsl_get_length(type);
      for (unsigned i = 0; i < length; i++) {
         const struct glsl_type *child_type = glsl_get_struct_field(type, i);
         add_var_xfb_outputs(xfb, varyings, var, buffer, location, offset,
                             child_type, varying_added);
      }
   } else {
      assert(buffer < NIR_MAX_XFB_BUFFERS);
      if (xfb->buffers_written & (1 << buffer)) {
         assert(xfb->buffers[buffer].stride == var->data.xfb.stride);
         assert(xfb->buffer_to_stream[buffer] == var->data.stream);
      } else {
         xfb->buffers_written |= (1 << buffer);
         xfb->buffers[buffer].stride = var->data.xfb.stride;
         xfb->buffer_to_stream[buffer] = var->data.stream;
      }

      assert(var->data.stream < NIR_MAX_XFB_STREAMS);
      xfb->streams_written |= (1 << var->data.stream);

      unsigned comp_slots;
      if (var->data.compact) {
         /* This only happens for clip/cull which are float arrays */
         assert(glsl_without_array(type) == glsl_float_type());
         assert(var->data.location == VARYING_SLOT_CLIP_DIST0 ||
                var->data.location == VARYING_SLOT_CLIP_DIST1);
         comp_slots = glsl_get_length(type);
      } else {
         comp_slots = glsl_get_component_slots(type);

         UNUSED unsigned attrib_slots = DIV_ROUND_UP(comp_slots, 4);
         assert(attrib_slots == glsl_count_attribute_slots(type, false));

         /* Ensure that we don't have, for instance, a dvec2 with a
          * location_frac of 2 which would make it crass a location boundary
          * even though it fits in a single slot.  However, you can have a
          * dvec3 which crosses the slot boundary with a location_frac of 2.
          */
         assert(DIV_ROUND_UP(var->data.location_frac + comp_slots, 4) ==
                attrib_slots);
      }

      assert(var->data.location_frac + comp_slots <= 8);
      uint8_t comp_mask = ((1 << comp_slots) - 1) << var->data.location_frac;
      unsigned comp_offset = var->data.location_frac;

      if (!varying_added) {
         add_var_xfb_varying(xfb, varyings, buffer, *offset, type);
      }

      while (comp_mask) {
         nir_xfb_output_info *output = &xfb->outputs[xfb->output_count++];

         output->buffer = buffer;
         output->offset = *offset;
         output->location = *location;
         output->component_mask = comp_mask & 0xf;
         output->component_offset = comp_offset;

         *offset += util_bitcount(output->component_mask) * 4;
         (*location)++;
         comp_mask >>= 4;
         comp_offset = 0;
      }
   }
}

static int
compare_xfb_varying_offsets(const void *_a, const void *_b)
{
   const nir_xfb_varying_info *a = _a, *b = _b;

   if (a->buffer != b->buffer)
      return a->buffer - b->buffer;

   return a->offset - b->offset;
}

static int
compare_xfb_output_offsets(const void *_a, const void *_b)
{
   const nir_xfb_output_info *a = _a, *b = _b;

   return a->offset - b->offset;
}

nir_xfb_info *
nir_gather_xfb_info(const nir_shader *shader, void *mem_ctx)
{
   return nir_gather_xfb_info_with_varyings(shader, mem_ctx, NULL);
}

nir_xfb_info *
nir_gather_xfb_info_with_varyings(const nir_shader *shader,
                                  void *mem_ctx,
                                  nir_xfb_varyings_info **varyings_info_out)
{
   assert(shader->info.stage == MESA_SHADER_VERTEX ||
          shader->info.stage == MESA_SHADER_TESS_EVAL ||
          shader->info.stage == MESA_SHADER_GEOMETRY);

   /* Compute the number of outputs we have.  This is simply the number of
    * cumulative locations consumed by all the variables.  If a location is
    * represented by multiple variables, then they each count separately in
    * number of outputs.  This is only an estimate as some variables may have
    * an xfb_buffer but not an output so it may end up larger than we need but
    * it should be good enough for allocation.
    */
   unsigned num_outputs = 0;
   unsigned num_varyings = 0;
   nir_xfb_varyings_info *varyings_info = NULL;
   nir_foreach_shader_out_variable(var, shader) {
      if (var->data.explicit_xfb_buffer) {
         num_outputs += glsl_count_attribute_slots(var->type, false);
         num_varyings += glsl_varying_count(var->type);
      }
   }
   if (num_outputs == 0 || num_varyings == 0)
      return NULL;

   nir_xfb_info *xfb = nir_xfb_info_create(mem_ctx, num_outputs);
   if (varyings_info_out != NULL) {
      *varyings_info_out = nir_xfb_varyings_info_create(mem_ctx, num_varyings);
      varyings_info = *varyings_info_out;
   }

   /* Walk the list of outputs and add them to the array */
   nir_foreach_shader_out_variable(var, shader) {
      if (!var->data.explicit_xfb_buffer)
         continue;

      unsigned location = var->data.location;

      /* In order to know if we have a array of blocks can't be done just by
       * checking if we have an interface type and is an array, because due
       * splitting we could end on a case were we received a split struct
       * that contains an array.
       */
      bool is_array_block = var->interface_type != NULL &&
         glsl_type_is_array(var->type) &&
         glsl_without_array(var->type) == var->interface_type;

      if (var->data.explicit_offset && !is_array_block) {
         unsigned offset = var->data.offset;
         add_var_xfb_outputs(xfb, varyings_info, var, var->data.xfb.buffer,
                             &location, &offset, var->type, false);
      } else if (is_array_block) {
         assert(glsl_type_is_struct_or_ifc(var->interface_type));

         unsigned aoa_size = glsl_get_aoa_size(var->type);
         const struct glsl_type *itype = var->interface_type;
         unsigned nfields = glsl_get_length(itype);
         for (unsigned b = 0; b < aoa_size; b++) {
            for (unsigned f = 0; f < nfields; f++) {
               int foffset = glsl_get_struct_field_offset(itype, f);
               const struct glsl_type *ftype = glsl_get_struct_field(itype, f);
               if (foffset < 0) {
                  location += glsl_count_attribute_slots(ftype, false);
                  continue;
               }

               unsigned offset = foffset;
               add_var_xfb_outputs(xfb, varyings_info, var, var->data.xfb.buffer + b,
                                   &location, &offset, ftype, false);
            }
         }
      }
   }

   /* Everything is easier in the state setup code if outputs and varyings are
    * sorted in order of output offset (and buffer for varyings).
    */
   qsort(xfb->outputs, xfb->output_count, sizeof(xfb->outputs[0]),
         compare_xfb_output_offsets);

   if (varyings_info != NULL) {
      qsort(varyings_info->varyings, varyings_info->varying_count,
            sizeof(varyings_info->varyings[0]),
            compare_xfb_varying_offsets);
   }

#ifndef NDEBUG
   /* Finally, do a sanity check */
   unsigned max_offset[NIR_MAX_XFB_BUFFERS] = {0};
   for (unsigned i = 0; i < xfb->output_count; i++) {
      assert(xfb->outputs[i].offset >= max_offset[xfb->outputs[i].buffer]);
      assert(xfb->outputs[i].component_mask != 0);
      unsigned slots = util_bitcount(xfb->outputs[i].component_mask);
      max_offset[xfb->outputs[i].buffer] = xfb->outputs[i].offset + slots * 4;
   }
#endif

   return xfb;
}
