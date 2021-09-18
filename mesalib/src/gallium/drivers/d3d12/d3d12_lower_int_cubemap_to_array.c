/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "d3d12_nir_passes.h"

#include "nir_builder.h"
#include "nir_builtin_builder.h"

static bool
lower_int_cubmap_to_array_filter(const nir_instr *instr,
                                 UNUSED const void *_options)
{
   if (instr->type != nir_instr_type_tex)
      return false;

   nir_tex_instr *tex = nir_instr_as_tex(instr);

   if (tex->sampler_dim != GLSL_SAMPLER_DIM_CUBE)
      return false;

   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txd:
   case nir_texop_txl:
   case nir_texop_txs:
   case nir_texop_lod:
      break;
   default:
      return false;
   }

   int sampler_deref = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   assert(sampler_deref >= 0);
   nir_deref_instr *deref = nir_instr_as_deref(tex->src[sampler_deref].src.ssa->parent_instr);
   nir_variable *cube = nir_deref_instr_get_variable(deref);
   return glsl_base_type_is_integer(glsl_get_sampler_result_type(cube->type));
}

typedef struct {
   nir_ssa_def *rx;
   nir_ssa_def *ry;
   nir_ssa_def *rz;
   nir_ssa_def *arx;
   nir_ssa_def *ary;
   nir_ssa_def *arz;
} coord_t;


/* This is taken from from sp_tex_sample:convert_cube */
static nir_ssa_def *
evaluate_face_x(nir_builder *b, coord_t *coord)
{
   nir_ssa_def *sign = nir_fsign(b, coord->rx);
   nir_ssa_def *positive = nir_fge(b, coord->rx, nir_imm_float(b, 0.0));
   nir_ssa_def *ima = nir_fdiv(b, nir_imm_float(b, -0.5), coord->arx);

   nir_ssa_def *x = nir_fadd(b, nir_fmul(b, nir_fmul(b, sign, ima), coord->rz), nir_imm_float(b, 0.5));
   nir_ssa_def *y = nir_fadd(b, nir_fmul(b, ima, coord->ry), nir_imm_float(b, 0.5));
   nir_ssa_def *face = nir_bcsel(b, positive, nir_imm_float(b, 0.0), nir_imm_float(b, 1.0));

   return nir_vec3(b, x,y, face);
}

static nir_ssa_def *
evaluate_face_y(nir_builder *b, coord_t *coord)
{
   nir_ssa_def *sign = nir_fsign(b, coord->ry);
   nir_ssa_def *positive = nir_fge(b, coord->ry, nir_imm_float(b, 0.0));
   nir_ssa_def *ima = nir_fdiv(b, nir_imm_float(b, 0.5), coord->ary);

   nir_ssa_def *x = nir_fadd(b, nir_fmul(b, ima, coord->rx), nir_imm_float(b, 0.5));
   nir_ssa_def *y = nir_fadd(b, nir_fmul(b, nir_fmul(b, sign, ima), coord->rz), nir_imm_float(b, 0.5));
   nir_ssa_def *face = nir_bcsel(b, positive, nir_imm_float(b, 2.0), nir_imm_float(b, 3.0));

   return nir_vec3(b, x,y, face);
}

static nir_ssa_def *
evaluate_face_z(nir_builder *b, coord_t *coord)
{
   nir_ssa_def *sign = nir_fsign(b, coord->rz);
   nir_ssa_def *positive = nir_fge(b, coord->rz, nir_imm_float(b, 0.0));
   nir_ssa_def *ima = nir_fdiv(b, nir_imm_float(b, -0.5), coord->arz);

   nir_ssa_def *x = nir_fadd(b, nir_fmul(b, nir_fmul(b, sign, ima), nir_fneg(b, coord->rx)), nir_imm_float(b, 0.5));
   nir_ssa_def *y = nir_fadd(b, nir_fmul(b, ima, coord->ry), nir_imm_float(b, 0.5));
   nir_ssa_def *face = nir_bcsel(b, positive, nir_imm_float(b, 4.0), nir_imm_float(b, 5.0));

   return nir_vec3(b, x,y, face);
}

static nir_ssa_def *
create_array_tex_from_cube_tex(nir_builder *b, nir_tex_instr *tex, nir_ssa_def *coord)
{
   nir_tex_instr *array_tex;

   array_tex = nir_tex_instr_create(b->shader, tex->num_srcs);
   array_tex->op = tex->op;
   array_tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
   array_tex->is_array = true;
   array_tex->is_shadow = tex->is_shadow;
   array_tex->is_new_style_shadow = tex->is_new_style_shadow;
   array_tex->texture_index = tex->texture_index;
   array_tex->sampler_index = tex->sampler_index;
   array_tex->dest_type = tex->dest_type;
   array_tex->coord_components = 3;

   nir_src coord_src = nir_src_for_ssa(coord);
   for (unsigned i = 0; i < tex->num_srcs; i++) {
      nir_src *psrc = (tex->src[i].src_type == nir_tex_src_coord) ?
                         &coord_src : &tex->src[i].src;

      nir_src_copy(&array_tex->src[i].src, psrc);
      array_tex->src[i].src_type = tex->src[i].src_type;
   }

   nir_ssa_dest_init(&array_tex->instr, &array_tex->dest,
                     nir_tex_instr_dest_size(array_tex), 32, NULL);
   nir_builder_instr_insert(b, &array_tex->instr);
   return &array_tex->dest.ssa;
}

static nir_ssa_def *
lower_cube_sample(nir_builder *b, nir_tex_instr *tex)
{
   /* We don't support cube map arrays yet */
   assert(!tex->is_array);

   int coord_index = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   assert(coord_index >= 0);

   /* Evaluate the face and the xy coordinates for a 2D tex op */
   nir_ssa_def *coord = tex->src[coord_index].src.ssa;

   coord_t coords;
   coords.rx = nir_channel(b, coord, 0);
   coords.ry = nir_channel(b, coord, 1);
   coords.rz = nir_channel(b, coord, 2);
   coords.arx = nir_fabs(b, coords.rx);
   coords.ary = nir_fabs(b, coords.ry);
   coords.arz = nir_fabs(b, coords.rz);

   nir_ssa_def *use_face_x = nir_iand(b,
                                      nir_fge(b, coords.arx, coords.ary),
                                      nir_fge(b, coords.arx, coords.arz));

   nir_if *use_face_x_if = nir_push_if(b, use_face_x);
   nir_ssa_def *face_x_coord = evaluate_face_x(b, &coords);
   nir_if *use_face_x_else = nir_push_else(b, use_face_x_if);

   nir_ssa_def *use_face_y = nir_iand(b,
                                      nir_fge(b, coords.ary, coords.arx),
                                      nir_fge(b, coords.ary, coords.arz));

   nir_if *use_face_y_if = nir_push_if(b, use_face_y);
   nir_ssa_def *face_y_coord = evaluate_face_y(b, &coords);
   nir_if *use_face_y_else = nir_push_else(b, use_face_y_if);

   nir_ssa_def *face_z_coord = evaluate_face_z(b, &coords);

   nir_pop_if(b, use_face_y_else);
   nir_ssa_def *face_y_or_z_coord = nir_if_phi(b, face_y_coord, face_z_coord);
   nir_pop_if(b, use_face_x_else);

   // This contains in xy the normalized sample coordinates, and in z the face index
   nir_ssa_def *coord_and_face = nir_if_phi(b, face_x_coord, face_y_or_z_coord);

   return create_array_tex_from_cube_tex(b, tex, coord_and_face);
}

/* We don't expect the array size here */
static nir_ssa_def *
lower_cube_txs(nir_builder *b, nir_tex_instr *tex)
{
   b->cursor = nir_after_instr(&tex->instr);
   return nir_channels(b, &tex->dest.ssa, 3);
}

static const struct glsl_type *
make_2darray_from_cubemap(const struct glsl_type *type)
{
   return  glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE ?
            glsl_sampler_type(
               GLSL_SAMPLER_DIM_2D,
               false, true,
               glsl_get_sampler_result_type(type)) : type;
}

static const struct glsl_type *
make_2darray_from_cubemap_with_array(const struct glsl_type *type)
{
   /* While we don't (yet) support cube map arrays, there still may be arrays
    * of cube maps */
   if (glsl_type_is_array(type)) {
      const struct glsl_type *new_type = glsl_without_array(type);
      return new_type != type ? glsl_array_type(make_2darray_from_cubemap(glsl_without_array(type)),
                                                glsl_get_length(type), 0) : type;
   } else
      return make_2darray_from_cubemap(type);
}

static nir_ssa_def *
lower_int_cubmap_to_array_impl(nir_builder *b, nir_instr *instr,
                               UNUSED void *_options)
{
   nir_tex_instr *tex = nir_instr_as_tex(instr);

   int sampler_index = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   assert(sampler_index >= 0);

   nir_deref_instr *sampler_deref = nir_instr_as_deref(tex->src[sampler_index].src.ssa->parent_instr);
   nir_variable *sampler = nir_deref_instr_get_variable(sampler_deref);

   sampler->type = make_2darray_from_cubemap_with_array(sampler->type);
   sampler_deref->type = sampler->type;

   switch (tex->op) {
   case nir_texop_tex:
   case nir_texop_txb:
   case nir_texop_txd:
   case nir_texop_txl:
   case nir_texop_lod:
      return lower_cube_sample(b, tex);
   case nir_texop_txs:
      return lower_cube_txs(b, tex);
   default:
      unreachable("Unsupported cupe map texture operation");
   }
}

bool
d3d12_lower_int_cubmap_to_array(nir_shader *s)
{
   bool result =
         nir_shader_lower_instructions(s,
                                       lower_int_cubmap_to_array_filter,
                                       lower_int_cubmap_to_array_impl,
                                       NULL);

   if (result) {
      nir_foreach_variable_with_modes_safe(var, s, nir_var_uniform) {
         if (glsl_type_is_sampler(var->type)) {
            if (glsl_get_sampler_dim(var->type) == GLSL_SAMPLER_DIM_CUBE &&
                (glsl_base_type_is_integer(glsl_get_sampler_result_type(var->type)))) {
               var->type = make_2darray_from_cubemap_with_array(var->type);
            }
         }
      }
   }
   return result;

}
