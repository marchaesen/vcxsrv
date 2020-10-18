/*
 * Copyright © 2015 Red Hat
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

#include "nir.h"
#include "nir_builder.h"

static void
lower_impl(nir_function_impl *impl)
{
   nir_shader *shader = impl->function->shader;
   nir_builder b;
   nir_variable *in, *out;
   nir_ssa_def *def;

   nir_builder_init(&b, impl);
   b.cursor = nir_before_cf_list(&impl->body);

   /* The edge flag is the last input in st/mesa. */
   assert(shader->num_inputs == util_bitcount64(shader->info.inputs_read));

   /* Lowered IO only uses intrinsics. It doesn't use variables. */
   if (shader->info.io_lowered) {
      assert(shader->num_outputs ==
             util_bitcount64(shader->info.outputs_written));

      /* Load an edge flag. */
      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(shader, nir_intrinsic_load_input);
      load->num_components = 1;
      load->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
      nir_ssa_dest_init(&load->instr, &load->dest,
                        load->num_components, 32, NULL);

      nir_intrinsic_set_base(load, shader->num_inputs++);
      nir_intrinsic_set_component(load, 0);
      nir_intrinsic_set_dest_type(load, nir_type_float32);

      nir_io_semantics load_sem = {0};
      load_sem.location = VERT_ATTRIB_EDGEFLAG;
      load_sem.num_slots = 1;
      nir_intrinsic_set_io_semantics(load, load_sem);
      nir_builder_instr_insert(&b, &load->instr);

      /* Store an edge flag. */
      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(shader, nir_intrinsic_store_output);
      store->num_components = 1;
      store->src[0] = nir_src_for_ssa(&load->dest.ssa);
      store->src[1] = nir_src_for_ssa(nir_imm_int(&b, 0));

      nir_intrinsic_set_base(store, shader->num_outputs++);
      nir_intrinsic_set_component(store, 0);
      nir_intrinsic_set_src_type(store, nir_type_float32);
      nir_intrinsic_set_write_mask(store, 0x1);

      nir_io_semantics semantics = {0};
      semantics.location = VARYING_SLOT_EDGE;
      semantics.num_slots = 1;
      nir_intrinsic_set_io_semantics(store, semantics);
      nir_builder_instr_insert(&b, &store->instr);

      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
      return;
   }

   in  = nir_variable_create(shader, nir_var_shader_in,
                             glsl_vec4_type(), "edgeflag_in");
   in->data.location = VERT_ATTRIB_EDGEFLAG;

   in->data.driver_location = shader->num_inputs++;
   shader->info.inputs_read |= BITFIELD64_BIT(VERT_ATTRIB_EDGEFLAG);

   out = nir_variable_create(shader, nir_var_shader_out,
                             glsl_vec4_type(), "edgeflag_out");
   out->data.location = VARYING_SLOT_EDGE;

   def = nir_load_var(&b, in);
   nir_store_var(&b, out, def, 0xf);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void nir_lower_passthrough_edgeflags(nir_shader *shader)
{
   lower_impl(nir_shader_get_entrypoint(shader));
}
