/*
 * Copyright © 2020 Intel Corporation
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

#include "nir.h"
#include "nir_builder.h"

static bool
lower_cl_images_to_tex_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         unsigned num_srcs;
         nir_texop texop;
         switch (intrin->intrinsic) {
         case nir_intrinsic_image_deref_load:
            texop = nir_texop_txf;
            num_srcs = 3;
            break;
         case nir_intrinsic_image_deref_size:
            texop = nir_texop_txs;
            num_srcs = 2;
            break;
         default:
            /* Not an op we can lower */
            continue;
         }

         /* In CL 1.2, images are required to be either read-only or
          * write-only.  We can always translate the read-only image ops to
          * texture ops.  In CL 2.0 (and an extension), the ability is added
          * to have read-write images but sampling (with a sampler) is only
          * allowed on read-only images.  As long as we only lower read-only
          * images to texture ops, everything should stay consistent.
          */
         if (!(nir_intrinsic_access(intrin) & ACCESS_NON_WRITEABLE))
            continue;

         nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);

         b.cursor = nir_instr_remove(&intrin->instr);

         nir_tex_instr *tex = nir_tex_instr_create(b.shader, num_srcs);
         tex->op = texop;

         tex->sampler_dim = glsl_get_sampler_dim(deref->type);
         tex->is_array = glsl_sampler_type_is_array(deref->type);
         tex->is_shadow = false;

         unsigned coord_components =
            glsl_get_sampler_dim_coordinate_components(tex->sampler_dim);
         if (glsl_sampler_type_is_array(deref->type))
            tex->coord_components++;

         tex->src[0].src_type = nir_tex_src_texture_deref;
         tex->src[0].src = nir_src_for_ssa(&deref->dest.ssa);

         switch (intrin->intrinsic) {
         case nir_intrinsic_image_deref_load: {
            assert(intrin->src[1].is_ssa);
            tex->coord_components = coord_components;
            nir_ssa_def *coord =
               nir_channels(&b, intrin->src[1].ssa,
                            (1 << tex->coord_components) - 1);
            tex->src[1].src_type = nir_tex_src_coord;
            tex->src[1].src = nir_src_for_ssa(coord);

            assert(intrin->src[3].is_ssa);
            nir_ssa_def *lod = intrin->src[3].ssa;
            tex->src[2].src_type = nir_tex_src_lod;
            tex->src[2].src = nir_src_for_ssa(lod);

            assert(num_srcs == 3);

            tex->dest_type = nir_intrinsic_dest_type(intrin);
            nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);
            break;
         }

         case nir_intrinsic_image_deref_size: {
            assert(intrin->src[1].is_ssa);
            nir_ssa_def *lod = intrin->src[1].ssa;
            tex->src[1].src_type = nir_tex_src_lod;
            tex->src[1].src = nir_src_for_ssa(lod);

            assert(num_srcs == 2);

            tex->dest_type = nir_type_uint32;
            nir_ssa_dest_init(&tex->instr, &tex->dest,
                              coord_components, 32, NULL);
            break;
         }

         default:
            unreachable("Unsupported intrinsic");
         }

         nir_builder_instr_insert(&b, &tex->instr);

         nir_ssa_def *res = &tex->dest.ssa;
         if (res->num_components != intrin->dest.ssa.num_components) {
            unsigned num_components = intrin->dest.ssa.num_components;
            res = nir_channels(&b, res, (1 << num_components) - 1);
         }

         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(res));
         progress = true;
      }
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   } else {
      nir_metadata_preserve(impl, nir_metadata_all);
   }

   return progress;
}

/** Lowers OpenCL image ops to texture ops for read-only images */
bool
nir_lower_cl_images_to_tex(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl && lower_cl_images_to_tex_impl(function->impl))
         progress = true;
   }

   return progress;
}
