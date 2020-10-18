/*
 * Copyright © 2016 Intel Corporation
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

static nir_ssa_def *
load_frag_coord(const nir_input_attachment_options *options, nir_builder *b)
{
   if (options->use_fragcoord_sysval)
      return nir_load_frag_coord(b);

   nir_variable *pos =
      nir_find_variable_with_location(b->shader, nir_var_shader_in,
                                      VARYING_SLOT_POS);
   if (pos == NULL) {
      pos = nir_variable_create(b->shader, nir_var_shader_in,
                                glsl_vec4_type(), NULL);
      pos->data.location = VARYING_SLOT_POS;
   }
   /**
    * From Vulkan spec:
    *   "The OriginLowerLeft execution mode must not be used; fragment entry
    *    points must declare OriginUpperLeft."
    *
    * So at this point origin_upper_left should be true
    */
   assert(b->shader->info.fs.origin_upper_left == true);

   return nir_load_var(b, pos);
}

static nir_ssa_def *
load_layer_id(const nir_input_attachment_options *options, nir_builder *b)
{
   if (options->use_layer_id_sysval) {
      if (options->use_view_id_for_layer)
         return nir_load_view_index(b);
      else
         return nir_load_layer_id(b);
   }

   gl_varying_slot slot = options->use_view_id_for_layer ?
      VARYING_SLOT_VIEW_INDEX : VARYING_SLOT_LAYER;
   nir_variable *layer_id =
      nir_find_variable_with_location(b->shader, nir_var_shader_in, slot);

   if (layer_id == NULL) {
      layer_id = nir_variable_create(b->shader, nir_var_shader_in,
                                     glsl_int_type(), NULL);
      layer_id->data.location = slot;
      layer_id->data.interpolation = INTERP_MODE_FLAT;
      layer_id->data.driver_location = b->shader->num_inputs++;
   }

   return nir_load_var(b, layer_id);
}

static bool
try_lower_input_load(nir_function_impl *impl, nir_intrinsic_instr *load,
                     const nir_input_attachment_options *options)
{
   nir_deref_instr *deref = nir_src_as_deref(load->src[0]);
   assert(glsl_type_is_image(deref->type));

   enum glsl_sampler_dim image_dim = glsl_get_sampler_dim(deref->type);
   if (image_dim != GLSL_SAMPLER_DIM_SUBPASS &&
       image_dim != GLSL_SAMPLER_DIM_SUBPASS_MS)
      return false;

   const bool multisampled = (image_dim == GLSL_SAMPLER_DIM_SUBPASS_MS);

   nir_builder b;
   nir_builder_init(&b, impl);
   b.cursor = nir_instr_remove(&load->instr);

   nir_ssa_def *frag_coord = load_frag_coord(options, &b);
   frag_coord = nir_f2i32(&b, frag_coord);
   nir_ssa_def *offset = nir_ssa_for_src(&b, load->src[1], 2);
   nir_ssa_def *pos = nir_iadd(&b, frag_coord, offset);

   nir_ssa_def *layer = load_layer_id(options, &b);
   nir_ssa_def *coord =
      nir_vec3(&b, nir_channel(&b, pos, 0), nir_channel(&b, pos, 1), layer);

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3 + multisampled);

   tex->op = nir_texop_txf;
   tex->sampler_dim = image_dim;

   switch (glsl_get_sampler_result_type(deref->type)) {
   case GLSL_TYPE_FLOAT:
      tex->dest_type = nir_type_float;
      break;
   case GLSL_TYPE_INT:
      tex->dest_type = nir_type_int;
      break;
   case GLSL_TYPE_UINT:
      tex->dest_type = nir_type_uint;
      break;
   default:
      unreachable("Invalid image type");
   }
   tex->is_array = true;
   tex->is_shadow = false;

   tex->texture_index = 0;
   tex->sampler_index = 0;

   tex->src[0].src_type = nir_tex_src_texture_deref;
   tex->src[0].src = nir_src_for_ssa(&deref->dest.ssa);

   tex->src[1].src_type = nir_tex_src_coord;
   tex->src[1].src = nir_src_for_ssa(coord);
   tex->coord_components = 3;

   tex->src[2].src_type = nir_tex_src_lod;
   tex->src[2].src = nir_src_for_ssa(nir_imm_int(&b, 0));

   if (image_dim == GLSL_SAMPLER_DIM_SUBPASS_MS) {
      tex->op = nir_texop_txf_ms;
      tex->src[3].src_type = nir_tex_src_ms_index;
      tex->src[3].src = load->src[2];
   }

   tex->texture_non_uniform = nir_intrinsic_access(load) & ACCESS_NON_UNIFORM;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, NULL);
   nir_builder_instr_insert(&b, &tex->instr);

   nir_ssa_def_rewrite_uses(&load->dest.ssa,
                            nir_src_for_ssa(&tex->dest.ssa));

   return true;
}

static bool
try_lower_input_texop(nir_function_impl *impl, nir_tex_instr *tex,
                      const nir_input_attachment_options *options)
{
   nir_deref_instr *deref = nir_src_as_deref(tex->src[0].src);

   if (glsl_get_sampler_dim(deref->type) != GLSL_SAMPLER_DIM_SUBPASS_MS)
      return false;

   nir_builder b;
   nir_builder_init(&b, impl);
   b.cursor = nir_before_instr(&tex->instr);

   nir_ssa_def *frag_coord = load_frag_coord(options, &b);
   frag_coord = nir_f2i32(&b, frag_coord);

   nir_ssa_def *layer = load_layer_id(options, &b);
   nir_ssa_def *coord = nir_vec3(&b, nir_channel(&b, frag_coord, 0),
                                     nir_channel(&b, frag_coord, 1), layer);

   tex->coord_components = 3;

   nir_instr_rewrite_src(&tex->instr, &tex->src[1].src, nir_src_for_ssa(coord));

   return true;
}

bool
nir_lower_input_attachments(nir_shader *shader,
                            const nir_input_attachment_options *options)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            switch (instr->type) {
            case nir_instr_type_tex: {
               nir_tex_instr *tex = nir_instr_as_tex(instr);

               if (tex->op == nir_texop_fragment_mask_fetch ||
                   tex->op == nir_texop_fragment_fetch) {
                  progress |= try_lower_input_texop(function->impl, tex,
                                                    options);
               }
               break;
            }
            case nir_instr_type_intrinsic: {
               nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);

               if (load->intrinsic == nir_intrinsic_image_deref_load) {
                  progress |= try_lower_input_load(function->impl, load,
                                                   options);
               }
               break;
            }
            default:
               break;
            }
         }
      }
   }

   return progress;
}
