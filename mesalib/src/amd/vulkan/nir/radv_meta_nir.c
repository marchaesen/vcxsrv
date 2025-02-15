/* Based on anv:
 * Copyright © 2015 Intel Corporation
 *
 * Copyright © 2016 Red Hat Inc.
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */


#include "ac_surface.h"
#include "ac_nir_surface.h"
#include "nir/nir_format_convert.h"
#include "nir_builder.h"
#include "radv_meta_nir.h"
#include "radv_device.h"
#include "radv_physical_device.h"

nir_builder PRINTFLIKE(3, 4)
   radv_meta_nir_init_shader(struct radv_device *dev, gl_shader_stage stage, const char *name, ...)
{
   const struct radv_physical_device *pdev = radv_device_physical(dev);
   nir_builder b = nir_builder_init_simple_shader(stage, NULL, NULL);
   if (name) {
      va_list args;
      va_start(args, name);
      b.shader->info.name = ralloc_vasprintf(b.shader, name, args);
      va_end(args);
   }

   b.shader->options = &pdev->nir_options[stage];

   radv_device_associate_nir(dev, b.shader);

   return b;
}

/* vertex shader that generates vertices */
nir_shader *
radv_meta_nir_build_vs_generate_vertices(struct radv_device *dev)
{
   const struct glsl_type *vec4 = glsl_vec4_type();

   nir_variable *v_position;

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_VERTEX, "meta_vs_gen_verts");

   nir_def *outvec = nir_gen_rect_vertices(&b, NULL, NULL);

   v_position = nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   v_position->data.location = VARYING_SLOT_POS;

   nir_store_var(&b, v_position, outvec, 0xf);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_fs_noop(struct radv_device *dev)
{
   return radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_noop_fs").shader;
}

static void
radv_meta_nir_build_resolve_shader_core(struct radv_device *device, nir_builder *b, bool is_integer, int samples,
                                    nir_variable *input_img, nir_variable *color, nir_def *img_coord)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_deref_instr *input_img_deref = nir_build_deref_var(b, input_img);
   nir_def *sample0 = nir_txf_ms_deref(b, input_img_deref, img_coord, nir_imm_int(b, 0));

   if (is_integer || samples <= 1) {
      nir_store_var(b, color, sample0, 0xf);
      return;
   }

   if (pdev->use_fmask) {
      nir_def *all_same = nir_samples_identical_deref(b, input_img_deref, img_coord);
      nir_push_if(b, nir_inot(b, all_same));
   }

   nir_def *accum = sample0;
   for (int i = 1; i < samples; i++) {
      nir_def *sample = nir_txf_ms_deref(b, input_img_deref, img_coord, nir_imm_int(b, i));
      accum = nir_fadd(b, accum, sample);
   }

   accum = nir_fdiv_imm(b, accum, samples);
   nir_store_var(b, color, accum, 0xf);

   if (pdev->use_fmask) {
      nir_push_else(b, NULL);
      nir_store_var(b, color, sample0, 0xf);
      nir_pop_if(b, NULL);
   }
}

nir_def *
radv_meta_nir_load_descriptor(nir_builder *b, unsigned desc_set, unsigned binding)
{
   nir_def *rsrc = nir_vulkan_resource_index(b, 3, 32, nir_imm_int(b, 0), .desc_set = desc_set, .binding = binding);
   return nir_trim_vector(b, rsrc, 2);
}

nir_def *
radv_meta_nir_get_global_ids(nir_builder *b, unsigned num_components)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_def *block_ids = nir_channels(b, nir_load_workgroup_id(b), mask);
   nir_def *block_size =
      nir_channels(b,
                   nir_imm_ivec4(b, b->shader->info.workgroup_size[0], b->shader->info.workgroup_size[1],
                                 b->shader->info.workgroup_size[2], 0),
                   mask);

   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

void
radv_meta_nir_break_on_count(nir_builder *b, nir_variable *var, nir_def *count)
{
   nir_def *counter = nir_load_var(b, var);

   nir_break_if(b, nir_uge(b, counter, count));

   counter = nir_iadd_imm(b, counter, 1);
   nir_store_var(b, var, counter, 0x1);
}

nir_shader *
radv_meta_nir_build_buffer_fill_shader(struct radv_device *dev)
{
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_buffer_fill");
   b.shader->info.workgroup_size[0] = 64;

   nir_def *pconst = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *buffer_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst, 0b0011));
   nir_def *max_offset = nir_channel(&b, pconst, 2);
   nir_def *data = nir_swizzle(&b, nir_channel(&b, pconst, 3), (unsigned[]){0, 0, 0, 0}, 4);

   nir_def *global_id =
      nir_iadd(&b, nir_imul_imm(&b, nir_channel(&b, nir_load_workgroup_id(&b), 0), b.shader->info.workgroup_size[0]),
               nir_load_local_invocation_index(&b));

   nir_def *offset = nir_imin(&b, nir_imul_imm(&b, global_id, 16), max_offset);
   nir_def *dst_addr = nir_iadd(&b, buffer_addr, nir_u2u64(&b, offset));
   nir_build_store_global(&b, data, dst_addr, .align_mul = 4);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_buffer_copy_shader(struct radv_device *dev)
{
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_buffer_copy");
   b.shader->info.workgroup_size[0] = 64;

   nir_def *pconst = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *max_offset = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 16, .range = 4);
   nir_def *src_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst, 0b0011));
   nir_def *dst_addr = nir_pack_64_2x32(&b, nir_channels(&b, pconst, 0b1100));

   nir_def *global_id =
      nir_iadd(&b, nir_imul_imm(&b, nir_channel(&b, nir_load_workgroup_id(&b), 0), b.shader->info.workgroup_size[0]),
               nir_load_local_invocation_index(&b));

   nir_def *offset = nir_u2u64(&b, nir_imin(&b, nir_imul_imm(&b, global_id, 16), max_offset));

   nir_def *data = nir_build_load_global(&b, 4, 32, nir_iadd(&b, src_addr, offset), .align_mul = 4);
   nir_build_store_global(&b, data, nir_iadd(&b, dst_addr, offset), .align_mul = 4);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit_vertex_shader(struct radv_device *dev)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_VERTEX, "meta_blit_vs");

   nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   pos_out->data.location = VARYING_SLOT_POS;

   nir_variable *tex_pos_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "v_tex_pos");
   tex_pos_out->data.location = VARYING_SLOT_VAR0;
   tex_pos_out->data.interpolation = INTERP_MODE_SMOOTH;

   nir_def *outvec = nir_gen_rect_vertices(&b, NULL, NULL);

   nir_store_var(&b, pos_out, outvec, 0xf);

   nir_def *src_box = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *src0_z = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 0), .base = 16, .range = 4);

   nir_def *vertex_id = nir_load_vertex_id_zero_base(&b);

   /* vertex 0 - src0_x, src0_y, src0_z */
   /* vertex 1 - src0_x, src1_y, src0_z*/
   /* vertex 2 - src1_x, src0_y, src0_z */
   /* so channel 0 is vertex_id != 2 ? src_x : src_x + w
      channel 1 is vertex id != 1 ? src_y : src_y + w */

   nir_def *c0cmp = nir_ine_imm(&b, vertex_id, 2);
   nir_def *c1cmp = nir_ine_imm(&b, vertex_id, 1);

   nir_def *comp[4];
   comp[0] = nir_bcsel(&b, c0cmp, nir_channel(&b, src_box, 0), nir_channel(&b, src_box, 2));

   comp[1] = nir_bcsel(&b, c1cmp, nir_channel(&b, src_box, 1), nir_channel(&b, src_box, 3));
   comp[2] = src0_z;
   comp[3] = nir_imm_float(&b, 1.0);
   nir_def *out_tex_vec = nir_vec(&b, comp, 4);
   nir_store_var(&b, tex_pos_out, out_tex_vec, 0xf);
   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit_copy_fragment_shader(struct radv_device *dev, enum glsl_sampler_dim tex_dim)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_blit_fs.%d", tex_dim);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec4, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   /* Swizzle the array index which comes in as Z coordinate into the right
    * position.
    */
   unsigned swz[] = {0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2};
   nir_def *const tex_pos =
      nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3));

   const struct glsl_type *sampler_type =
      glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D, glsl_get_base_type(vec4));
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_deref_instr *tex_deref = nir_build_deref_var(&b, sampler);
   nir_def *color = nir_tex_deref(&b, tex_deref, tex_deref, tex_pos);

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, color_out, color, 0xf);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit_copy_fragment_shader_depth(struct radv_device *dev, enum glsl_sampler_dim tex_dim)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_blit_depth_fs.%d", tex_dim);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec4, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   /* Swizzle the array index which comes in as Z coordinate into the right
    * position.
    */
   unsigned swz[] = {0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2};
   nir_def *const tex_pos =
      nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3));

   const struct glsl_type *sampler_type =
      glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D, glsl_get_base_type(vec4));
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_deref_instr *tex_deref = nir_build_deref_var(&b, sampler);
   nir_def *color = nir_tex_deref(&b, tex_deref, tex_deref, tex_pos);

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DEPTH;
   nir_store_var(&b, color_out, color, 0x1);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit_copy_fragment_shader_stencil(struct radv_device *dev, enum glsl_sampler_dim tex_dim)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_blit_stencil_fs.%d", tex_dim);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec4, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   /* Swizzle the array index which comes in as Z coordinate into the right
    * position.
    */
   unsigned swz[] = {0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2};
   nir_def *const tex_pos =
      nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3));

   const struct glsl_type *sampler_type =
      glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D, glsl_get_base_type(vec4));
   nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_deref_instr *tex_deref = nir_build_deref_var(&b, sampler);
   nir_def *color = nir_tex_deref(&b, tex_deref, tex_deref, tex_pos);

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_STENCIL;
   nir_store_var(&b, color_out, color, 0x1);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit2d_vertex_shader(struct radv_device *device)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_VERTEX, "meta_blit2d_vs");

   nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "gl_Position");
   pos_out->data.location = VARYING_SLOT_POS;

   nir_variable *tex_pos_out = nir_variable_create(b.shader, nir_var_shader_out, vec2, "v_tex_pos");
   tex_pos_out->data.location = VARYING_SLOT_VAR0;
   tex_pos_out->data.interpolation = INTERP_MODE_SMOOTH;

   nir_def *outvec = nir_gen_rect_vertices(&b, NULL, NULL);
   nir_store_var(&b, pos_out, outvec, 0xf);

   nir_def *src_box = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *vertex_id = nir_load_vertex_id_zero_base(&b);

   /* vertex 0 - src_x, src_y */
   /* vertex 1 - src_x, src_y+h */
   /* vertex 2 - src_x+w, src_y */
   /* so channel 0 is vertex_id != 2 ? src_x : src_x + w
      channel 1 is vertex id != 1 ? src_y : src_y + w */

   nir_def *c0cmp = nir_ine_imm(&b, vertex_id, 2);
   nir_def *c1cmp = nir_ine_imm(&b, vertex_id, 1);

   nir_def *comp[2];
   comp[0] = nir_bcsel(&b, c0cmp, nir_channel(&b, src_box, 0), nir_channel(&b, src_box, 2));

   comp[1] = nir_bcsel(&b, c1cmp, nir_channel(&b, src_box, 1), nir_channel(&b, src_box, 3));
   nir_def *out_tex_vec = nir_vec(&b, comp, 2);
   nir_store_var(&b, tex_pos_out, out_tex_vec, 0x3);
   return b.shader;
}

nir_def *
radv_meta_nir_build_blit2d_texel_fetch(struct nir_builder *b, struct radv_device *device, nir_def *tex_pos, bool is_3d,
                                       bool is_multisampled)
{
   enum glsl_sampler_dim dim = is_3d             ? GLSL_SAMPLER_DIM_3D
                               : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                 : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *sampler_type = glsl_sampler_type(dim, false, false, GLSL_TYPE_UINT);
   nir_variable *sampler = nir_variable_create(b->shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_def *tex_pos_3d = NULL;
   nir_def *sample_idx = NULL;
   if (is_3d) {
      nir_def *layer = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0), .base = 16, .range = 4);

      nir_def *chans[3];
      chans[0] = nir_channel(b, tex_pos, 0);
      chans[1] = nir_channel(b, tex_pos, 1);
      chans[2] = layer;
      tex_pos_3d = nir_vec(b, chans, 3);
   }
   if (is_multisampled) {
      sample_idx = nir_load_sample_id(b);
   }

   nir_deref_instr *tex_deref = nir_build_deref_var(b, sampler);

   if (is_multisampled) {
      return nir_txf_ms_deref(b, tex_deref, tex_pos, sample_idx);
   } else {
      return nir_txf_deref(b, tex_deref, is_3d ? tex_pos_3d : tex_pos, NULL);
   }
}

nir_def *
radv_meta_nir_build_blit2d_buffer_fetch(struct nir_builder *b, struct radv_device *device, nir_def *tex_pos, bool is_3d,
                                        bool is_multisampled)
{
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_UINT);
   nir_variable *sampler = nir_variable_create(b->shader, nir_var_uniform, sampler_type, "s_tex");
   sampler->data.descriptor_set = 0;
   sampler->data.binding = 0;

   nir_def *width = nir_load_push_constant(b, 1, 32, nir_imm_int(b, 0), .base = 16, .range = 4);

   nir_def *pos_x = nir_channel(b, tex_pos, 0);
   nir_def *pos_y = nir_channel(b, tex_pos, 1);
   pos_y = nir_imul(b, pos_y, width);
   pos_x = nir_iadd(b, pos_x, pos_y);

   nir_deref_instr *tex_deref = nir_build_deref_var(b, sampler);
   return nir_txf_deref(b, tex_deref, pos_x, NULL);
}

nir_shader *
radv_meta_nir_build_blit2d_copy_fragment_shader(struct radv_device *device,
                                                radv_meta_nir_texel_fetch_build_func txf_func, const char *name,
                                                bool is_3d, bool is_multisampled)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_FRAGMENT, "%s", name);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec2, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DATA0;

   nir_def *pos_int = nir_f2i32(&b, nir_load_var(&b, tex_pos_in));
   nir_def *tex_pos = nir_trim_vector(&b, pos_int, 2);

   nir_def *color = txf_func(&b, device, tex_pos, is_3d, is_multisampled);
   nir_store_var(&b, color_out, color, 0xf);

   b.shader->info.fs.uses_sample_shading = is_multisampled;

   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit2d_copy_fragment_shader_depth(struct radv_device *device,
                                                      radv_meta_nir_texel_fetch_build_func txf_func, const char *name,
                                                      bool is_3d, bool is_multisampled)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_FRAGMENT, "%s", name);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec2, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DEPTH;

   nir_def *pos_int = nir_f2i32(&b, nir_load_var(&b, tex_pos_in));
   nir_def *tex_pos = nir_trim_vector(&b, pos_int, 2);

   nir_def *color = txf_func(&b, device, tex_pos, is_3d, is_multisampled);
   nir_store_var(&b, color_out, color, 0x1);

   b.shader->info.fs.uses_sample_shading = is_multisampled;

   return b.shader;
}

nir_shader *
radv_meta_nir_build_blit2d_copy_fragment_shader_stencil(struct radv_device *device,
                                                        radv_meta_nir_texel_fetch_build_func txf_func, const char *name,
                                                        bool is_3d, bool is_multisampled)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_FRAGMENT, "%s", name);

   nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec2, "v_tex_pos");
   tex_pos_in->data.location = VARYING_SLOT_VAR0;

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_STENCIL;

   nir_def *pos_int = nir_f2i32(&b, nir_load_var(&b, tex_pos_in));
   nir_def *tex_pos = nir_trim_vector(&b, pos_int, 2);

   nir_def *color = txf_func(&b, device, tex_pos, is_3d, is_multisampled);
   nir_store_var(&b, color_out, color, 0x1);

   b.shader->info.fs.uses_sample_shading = is_multisampled;

   return b.shader;
}

nir_shader *
radv_meta_nir_build_itob_compute_shader(struct radv_device *dev, bool is_3d)
{
   enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *sampler_type = glsl_sampler_type(dim, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, is_3d ? "meta_itob_cs_3d" : "meta_itob_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, is_3d ? 3 : 2);

   nir_def *offset = nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = is_3d ? 12 : 8);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *img_coord = nir_iadd(&b, global_id, offset);
   nir_def *outval =
      nir_txf_deref(&b, nir_build_deref_var(&b, input_img), nir_trim_vector(&b, img_coord, 2 + is_3d), NULL);

   nir_def *pos_x = nir_channel(&b, global_id, 0);
   nir_def *pos_y = nir_channel(&b, global_id, 1);

   nir_def *tmp = nir_imul(&b, pos_y, stride);
   tmp = nir_iadd(&b, tmp, pos_x);

   nir_def *coord = nir_replicate(&b, tmp, 4);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32), outval,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_btoi_compute_shader(struct radv_device *dev, bool is_3d)
{
   enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, is_3d ? "meta_btoi_cs_3d" : "meta_btoi_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, is_3d ? 3 : 2);

   nir_def *offset = nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = is_3d ? 12 : 8);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *pos_x = nir_channel(&b, global_id, 0);
   nir_def *pos_y = nir_channel(&b, global_id, 1);

   nir_def *buf_coord = nir_imul(&b, pos_y, stride);
   buf_coord = nir_iadd(&b, buf_coord, pos_x);

   nir_def *coord = nir_iadd(&b, global_id, offset);
   nir_def *outval = nir_txf_deref(&b, nir_build_deref_var(&b, input_img), buf_coord, NULL);

   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, coord, 0), nir_channel(&b, coord, 1),
                                 is_3d ? nir_channel(&b, coord, 2) : nir_undef(&b, 1, 32), nir_undef(&b, 1, 32));

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, img_coord, nir_undef(&b, 1, 32), outval,
                         nir_imm_int(&b, 0), .image_dim = dim);

   return b.shader;
}

/** Buffer to image - special path for R32G32B32 */
nir_shader *
radv_meta_nir_build_btoi_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_btoi_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);

   nir_def *offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);
   nir_def *pitch = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 8), .range = 12);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *pos_x = nir_channel(&b, global_id, 0);
   nir_def *pos_y = nir_channel(&b, global_id, 1);

   nir_def *buf_coord = nir_imul(&b, pos_y, stride);
   buf_coord = nir_iadd(&b, buf_coord, pos_x);

   nir_def *img_coord = nir_iadd(&b, global_id, offset);

   nir_def *global_pos = nir_iadd(&b, nir_imul(&b, nir_channel(&b, img_coord, 1), pitch),
                                  nir_imul_imm(&b, nir_channel(&b, img_coord, 0), 3));

   nir_def *outval = nir_txf_deref(&b, nir_build_deref_var(&b, input_img), buf_coord, NULL);

   for (int chan = 0; chan < 3; chan++) {
      nir_def *local_pos = nir_iadd_imm(&b, global_pos, chan);

      nir_def *coord = nir_replicate(&b, local_pos, 4);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32),
                            nir_channel(&b, outval, chan), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

nir_shader *
radv_meta_nir_build_itoi_compute_shader(struct radv_device *dev, bool src_3d, bool dst_3d, int samples)
{
   bool is_multisampled = samples > 1;
   enum glsl_sampler_dim src_dim = src_3d            ? GLSL_SAMPLER_DIM_3D
                                   : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                     : GLSL_SAMPLER_DIM_2D;
   enum glsl_sampler_dim dst_dim = dst_3d            ? GLSL_SAMPLER_DIM_3D
                                   : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                     : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *buf_type = glsl_sampler_type(src_dim, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(dst_dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_itoi_cs-%dd-%dd-%d", src_3d ? 3 : 2,
                                         dst_3d ? 3 : 2, samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, (src_3d || dst_3d) ? 3 : 2);

   nir_def *src_offset = nir_load_push_constant(&b, src_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = src_3d ? 12 : 8);
   nir_def *dst_offset = nir_load_push_constant(&b, dst_3d ? 3 : 2, 32, nir_imm_int(&b, 12), .range = dst_3d ? 24 : 20);

   nir_def *src_coord = nir_iadd(&b, global_id, src_offset);
   nir_deref_instr *input_img_deref = nir_build_deref_var(&b, input_img);

   nir_def *dst_coord = nir_iadd(&b, global_id, dst_offset);

   nir_def *tex_vals[8];
   if (is_multisampled) {
      for (uint32_t i = 0; i < samples; i++) {
         tex_vals[i] = nir_txf_ms_deref(&b, input_img_deref, nir_trim_vector(&b, src_coord, 2), nir_imm_int(&b, i));
      }
   } else {
      tex_vals[0] = nir_txf_deref(&b, input_img_deref, nir_trim_vector(&b, src_coord, 2 + src_3d), nir_imm_int(&b, 0));
   }

   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, dst_coord, 0), nir_channel(&b, dst_coord, 1),
                                 dst_3d ? nir_channel(&b, dst_coord, 2) : nir_undef(&b, 1, 32), nir_undef(&b, 1, 32));

   for (uint32_t i = 0; i < samples; i++) {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, img_coord, nir_imm_int(&b, i), tex_vals[i],
                            nir_imm_int(&b, 0), .image_dim = dst_dim);
   }

   return b.shader;
}

nir_shader *
radv_meta_nir_build_itoi_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_itoi_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, type, "input_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "output_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);

   nir_def *src_offset = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 0), .range = 12);
   nir_def *dst_offset = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 12), .range = 24);

   nir_def *src_stride = nir_channel(&b, src_offset, 2);
   nir_def *dst_stride = nir_channel(&b, dst_offset, 2);

   nir_def *src_img_coord = nir_iadd(&b, global_id, src_offset);
   nir_def *dst_img_coord = nir_iadd(&b, global_id, dst_offset);

   nir_def *src_global_pos = nir_iadd(&b, nir_imul(&b, nir_channel(&b, src_img_coord, 1), src_stride),
                                      nir_imul_imm(&b, nir_channel(&b, src_img_coord, 0), 3));

   nir_def *dst_global_pos = nir_iadd(&b, nir_imul(&b, nir_channel(&b, dst_img_coord, 1), dst_stride),
                                      nir_imul_imm(&b, nir_channel(&b, dst_img_coord, 0), 3));

   for (int chan = 0; chan < 3; chan++) {
      /* src */
      nir_def *src_local_pos = nir_iadd_imm(&b, src_global_pos, chan);
      nir_def *outval = nir_txf_deref(&b, nir_build_deref_var(&b, input_img), src_local_pos, NULL);

      /* dst */
      nir_def *dst_local_pos = nir_iadd_imm(&b, dst_global_pos, chan);

      nir_def *dst_coord = nir_replicate(&b, dst_local_pos, 4);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, dst_coord, nir_undef(&b, 1, 32),
                            nir_channel(&b, outval, 0), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

nir_shader *
radv_meta_nir_build_cleari_compute_shader(struct radv_device *dev, bool is_3d, int samples)
{
   bool is_multisampled = samples > 1;
   enum glsl_sampler_dim dim = is_3d             ? GLSL_SAMPLER_DIM_3D
                               : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                 : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b =
      radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, is_3d ? "meta_cleari_cs_3d-%d" : "meta_cleari_cs-%d", samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);

   nir_def *clear_val = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *layer = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 16), .range = 20);

   nir_def *comps[4];
   comps[0] = nir_channel(&b, global_id, 0);
   comps[1] = nir_channel(&b, global_id, 1);
   comps[2] = layer;
   comps[3] = nir_undef(&b, 1, 32);
   global_id = nir_vec(&b, comps, 4);

   for (uint32_t i = 0; i < samples; i++) {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, global_id, nir_imm_int(&b, i), clear_val,
                            nir_imm_int(&b, 0), .image_dim = dim);
   }

   return b.shader;
}

/** Special path for clearing R32G32B32 images using a compute shader. */
nir_shader *
radv_meta_nir_build_cleari_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_cleari_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);

   nir_def *clear_val = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 0), .range = 12);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *global_x = nir_channel(&b, global_id, 0);
   nir_def *global_y = nir_channel(&b, global_id, 1);

   nir_def *global_pos = nir_iadd(&b, nir_imul(&b, global_y, stride), nir_imul_imm(&b, global_x, 3));

   for (unsigned chan = 0; chan < 3; chan++) {
      nir_def *local_pos = nir_iadd_imm(&b, global_pos, chan);

      nir_def *coord = nir_replicate(&b, local_pos, 4);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32),
                            nir_channel(&b, clear_val, chan), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

void
radv_meta_nir_build_clear_color_shaders(struct radv_device *dev, struct nir_shader **out_vs, struct nir_shader **out_fs,
                                        uint32_t frag_output)
{
   nir_builder vs_b = radv_meta_nir_init_shader(dev, MESA_SHADER_VERTEX, "meta_clear_color_vs");
   nir_builder fs_b = radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_clear_color_fs-%d", frag_output);

   const struct glsl_type *position_type = glsl_vec4_type();
   const struct glsl_type *color_type = glsl_vec4_type();

   nir_variable *vs_out_pos = nir_variable_create(vs_b.shader, nir_var_shader_out, position_type, "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_def *in_color_load = nir_load_push_constant(&fs_b, 4, 32, nir_imm_int(&fs_b, 0), .range = 16);

   nir_variable *fs_out_color = nir_variable_create(fs_b.shader, nir_var_shader_out, color_type, "f_color");
   fs_out_color->data.location = FRAG_RESULT_DATA0 + frag_output;

   nir_store_var(&fs_b, fs_out_color, in_color_load, 0xf);

   nir_def *outvec = nir_gen_rect_vertices(&vs_b, NULL, NULL);
   nir_store_var(&vs_b, vs_out_pos, outvec, 0xf);

   const struct glsl_type *layer_type = glsl_int_type();
   nir_variable *vs_out_layer = nir_variable_create(vs_b.shader, nir_var_shader_out, layer_type, "v_layer");
   vs_out_layer->data.location = VARYING_SLOT_LAYER;
   vs_out_layer->data.interpolation = INTERP_MODE_FLAT;
   nir_def *inst_id = nir_load_instance_id(&vs_b);
   nir_def *base_instance = nir_load_base_instance(&vs_b);

   nir_def *layer_id = nir_iadd(&vs_b, inst_id, base_instance);
   nir_store_var(&vs_b, vs_out_layer, layer_id, 0x1);

   *out_vs = vs_b.shader;
   *out_fs = fs_b.shader;
}

void
radv_meta_nir_build_clear_depthstencil_shaders(struct radv_device *dev, struct nir_shader **out_vs,
                                               struct nir_shader **out_fs, bool unrestricted)
{
   nir_builder vs_b = radv_meta_nir_init_shader(
      dev, MESA_SHADER_VERTEX, unrestricted ? "meta_clear_depthstencil_unrestricted_vs" : "meta_clear_depthstencil_vs");
   nir_builder fs_b =
      radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT,
                            unrestricted ? "meta_clear_depthstencil_unrestricted_fs" : "meta_clear_depthstencil_fs");

   const struct glsl_type *position_out_type = glsl_vec4_type();

   nir_variable *vs_out_pos = nir_variable_create(vs_b.shader, nir_var_shader_out, position_out_type, "gl_Position");
   vs_out_pos->data.location = VARYING_SLOT_POS;

   nir_def *z;
   if (unrestricted) {
      nir_def *in_color_load = nir_load_push_constant(&fs_b, 1, 32, nir_imm_int(&fs_b, 0), .range = 4);

      nir_variable *fs_out_depth = nir_variable_create(fs_b.shader, nir_var_shader_out, glsl_int_type(), "f_depth");
      fs_out_depth->data.location = FRAG_RESULT_DEPTH;
      nir_store_var(&fs_b, fs_out_depth, in_color_load, 0x1);

      z = nir_imm_float(&vs_b, 0.0);
   } else {
      z = nir_load_push_constant(&vs_b, 1, 32, nir_imm_int(&vs_b, 0), .range = 4);
   }

   nir_def *outvec = nir_gen_rect_vertices(&vs_b, z, NULL);
   nir_store_var(&vs_b, vs_out_pos, outvec, 0xf);

   const struct glsl_type *layer_type = glsl_int_type();
   nir_variable *vs_out_layer = nir_variable_create(vs_b.shader, nir_var_shader_out, layer_type, "v_layer");
   vs_out_layer->data.location = VARYING_SLOT_LAYER;
   vs_out_layer->data.interpolation = INTERP_MODE_FLAT;
   nir_def *inst_id = nir_load_instance_id(&vs_b);
   nir_def *base_instance = nir_load_base_instance(&vs_b);

   nir_def *layer_id = nir_iadd(&vs_b, inst_id, base_instance);
   nir_store_var(&vs_b, vs_out_layer, layer_id, 0x1);

   *out_vs = vs_b.shader;
   *out_fs = fs_b.shader;
}

nir_shader *
radv_meta_nir_build_clear_htile_mask_shader(struct radv_device *dev)
{
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_clear_htile_mask");
   b.shader->info.workgroup_size[0] = 64;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 1);

   nir_def *offset = nir_imul_imm(&b, global_id, 16);
   offset = nir_channel(&b, offset, 0);

   nir_def *constants = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *va = nir_pack_64_2x32(&b, nir_channels(&b, constants, 0x3));
   va = nir_iadd(&b, va, nir_u2u64(&b, offset));

   nir_def *load = nir_build_load_global(&b, 4, 32, va, .align_mul = 16);

   /* data = (data & ~htile_mask) | (htile_value & htile_mask) */
   nir_def *data = nir_iand(&b, load, nir_channel(&b, constants, 3));
   data = nir_ior(&b, data, nir_channel(&b, constants, 2));

   nir_build_store_global(&b, data, va, .access = ACCESS_NON_READABLE, .align_mul = 16);

   return b.shader;
}

/**
 * Clear DCC using comp-to-single by storing the clear value at the beginning of every 256B block.
 * For MSAA images, clearing the first sample should be enough as long as CMASK is also cleared.
 */
nir_shader *
radv_meta_nir_build_clear_dcc_comp_to_single_shader(struct radv_device *dev, bool is_msaa)
{
   enum glsl_sampler_dim dim = is_msaa ? GLSL_SAMPLER_DIM_MS : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *img_type = glsl_image_type(dim, true, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_clear_dcc_comp_to_single-%s",
                                         is_msaa ? "multisampled" : "singlesampled");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 3);

   /* Load the dimensions in pixels of a block that gets compressed to one DCC byte. */
   nir_def *dcc_block_size = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);

   /* Compute the coordinates. */
   nir_def *coord = nir_trim_vector(&b, global_id, 2);
   coord = nir_imul(&b, coord, dcc_block_size);
   coord = nir_vec4(&b, nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), nir_channel(&b, global_id, 2),
                    nir_undef(&b, 1, 32));

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   /* Load the clear color values. */
   nir_def *clear_values = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 8), .range = 24);

   nir_def *data = nir_vec4(&b, nir_channel(&b, clear_values, 0), nir_channel(&b, clear_values, 1),
                            nir_channel(&b, clear_values, 2), nir_channel(&b, clear_values, 3));

   /* Store the clear color values. */
   nir_def *sample_id = is_msaa ? nir_imm_int(&b, 0) : nir_undef(&b, 1, 32);
   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, sample_id, data, nir_imm_int(&b, 0),
                         .image_dim = dim, .image_array = true);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_copy_vrs_htile_shader(struct radv_device *device, struct radeon_surf *surf)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_COMPUTE, "meta_copy_vrs_htile");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   /* Get coordinates. */
   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);

   nir_def *addr = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);
   nir_def *htile_va = nir_pack_64_2x32(&b, nir_channels(&b, addr, 0x3));

   nir_def *offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 8), .range = 16);

   /* Multiply the coordinates by the HTILE block size. */
   nir_def *coord = nir_iadd(&b, nir_imul_imm(&b, global_id, 8), offset);

   /* Load constants. */
   nir_def *constants = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 16), .range = 28);
   nir_def *htile_pitch = nir_channel(&b, constants, 0);
   nir_def *htile_slice_size = nir_channel(&b, constants, 1);
   nir_def *read_htile_value = nir_channel(&b, constants, 2);

   /* Get the HTILE addr from coordinates. */
   nir_def *zero = nir_imm_int(&b, 0);
   nir_def *htile_offset =
      ac_nir_htile_addr_from_coord(&b, &pdev->info, &surf->u.gfx9.zs.htile_equation, htile_pitch, htile_slice_size,
                                   nir_channel(&b, coord, 0), nir_channel(&b, coord, 1), zero, zero);

   /* Set up the input VRS image descriptor. */
   const struct glsl_type *vrs_sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT);
   nir_variable *input_vrs_img = nir_variable_create(b.shader, nir_var_uniform, vrs_sampler_type, "input_vrs_image");
   input_vrs_img->data.descriptor_set = 0;
   input_vrs_img->data.binding = 0;

   /* Load the VRS rates from the 2D image. */
   nir_def *value = nir_txf_deref(&b, nir_build_deref_var(&b, input_vrs_img), global_id, NULL);

   /* Extract the X/Y rates and clamp them because the maximum supported VRS rate is 2x2 (1x1 in
    * hardware).
    *
    * VRS rate X = min(value >> 2, 1)
    * VRS rate Y = min(value & 3, 1)
    */
   nir_def *x_rate = nir_ushr_imm(&b, nir_channel(&b, value, 0), 2);
   x_rate = nir_umin(&b, x_rate, nir_imm_int(&b, 1));

   nir_def *y_rate = nir_iand_imm(&b, nir_channel(&b, value, 0), 3);
   y_rate = nir_umin(&b, y_rate, nir_imm_int(&b, 1));

   /* Compute the final VRS rate. */
   nir_def *vrs_rates = nir_ior(&b, nir_ishl_imm(&b, y_rate, 10), nir_ishl_imm(&b, x_rate, 6));

   /* Load the HTILE value if requested, otherwise use the default value. */
   nir_variable *htile_value = nir_local_variable_create(b.impl, glsl_int_type(), "htile_value");

   nir_push_if(&b, nir_ieq_imm(&b, read_htile_value, 1));
   {
      /* Load the existing HTILE 32-bit value for this 8x8 pixels area. */
      nir_def *input_value = nir_build_load_global(&b, 1, 32, nir_iadd(&b, htile_va, nir_u2u64(&b, htile_offset)));

      /* Clear the 4-bit VRS rates. */
      nir_store_var(&b, htile_value, nir_iand_imm(&b, input_value, 0xfffff33f), 0x1);
   }
   nir_push_else(&b, NULL);
   {
      nir_store_var(&b, htile_value, nir_imm_int(&b, 0xfffff33f), 0x1);
   }
   nir_pop_if(&b, NULL);

   /* Set the VRS rates loaded from the image. */
   nir_def *output_value = nir_ior(&b, nir_load_var(&b, htile_value), vrs_rates);

   /* Store the updated HTILE 32-bit which contains the VRS rates. */
   nir_build_store_global(&b, output_value, nir_iadd(&b, htile_va, nir_u2u64(&b, htile_offset)),
                          .access = ACCESS_NON_READABLE);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_dcc_retile_compute_shader(struct radv_device *dev, struct radeon_surf *surf)
{
   const struct radv_physical_device *pdev = radv_device_physical(dev);
   enum glsl_sampler_dim dim = GLSL_SAMPLER_DIM_BUF;
   const struct glsl_type *buf_type = glsl_image_type(dim, false, GLSL_TYPE_UINT);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "dcc_retile_compute");

   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_def *src_dcc_size = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);
   nir_def *src_dcc_pitch = nir_channels(&b, src_dcc_size, 1);
   nir_def *src_dcc_height = nir_channels(&b, src_dcc_size, 2);

   nir_def *dst_dcc_size = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 8), .range = 8);
   nir_def *dst_dcc_pitch = nir_channels(&b, dst_dcc_size, 1);
   nir_def *dst_dcc_height = nir_channels(&b, dst_dcc_size, 2);
   nir_variable *input_dcc = nir_variable_create(b.shader, nir_var_uniform, buf_type, "dcc_in");
   input_dcc->data.descriptor_set = 0;
   input_dcc->data.binding = 0;
   nir_variable *output_dcc = nir_variable_create(b.shader, nir_var_uniform, buf_type, "dcc_out");
   output_dcc->data.descriptor_set = 0;
   output_dcc->data.binding = 1;

   nir_def *input_dcc_ref = &nir_build_deref_var(&b, input_dcc)->def;
   nir_def *output_dcc_ref = &nir_build_deref_var(&b, output_dcc)->def;

   nir_def *coord = radv_meta_nir_get_global_ids(&b, 2);
   nir_def *zero = nir_imm_int(&b, 0);
   coord =
      nir_imul(&b, coord, nir_imm_ivec2(&b, surf->u.gfx9.color.dcc_block_width, surf->u.gfx9.color.dcc_block_height));

   nir_def *src = ac_nir_dcc_addr_from_coord(&b, &pdev->info, surf->bpe, &surf->u.gfx9.color.dcc_equation,
                                             src_dcc_pitch, src_dcc_height, zero, nir_channel(&b, coord, 0),
                                             nir_channel(&b, coord, 1), zero, zero, zero);
   nir_def *dst = ac_nir_dcc_addr_from_coord(&b, &pdev->info, surf->bpe, &surf->u.gfx9.color.display_dcc_equation,
                                             dst_dcc_pitch, dst_dcc_height, zero, nir_channel(&b, coord, 0),
                                             nir_channel(&b, coord, 1), zero, zero, zero);

   nir_def *dcc_val = nir_image_deref_load(&b, 1, 32, input_dcc_ref, nir_vec4(&b, src, src, src, src),
                                           nir_undef(&b, 1, 32), nir_imm_int(&b, 0), .image_dim = dim);

   nir_image_deref_store(&b, output_dcc_ref, nir_vec4(&b, dst, dst, dst, dst), nir_undef(&b, 1, 32), dcc_val,
                         nir_imm_int(&b, 0), .image_dim = dim);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_expand_depth_stencil_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "expand_depth_stencil_compute");

   /* We need at least 8/8/1 to cover an entire HTILE block in a single workgroup. */
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_image, img_type, "in_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_def *wg_id = nir_load_workgroup_id(&b);
   nir_def *block_size = nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                                       b.shader->info.workgroup_size[2], 0);

   nir_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_def *data = nir_image_deref_load(&b, 4, 32, &nir_build_deref_var(&b, input_img)->def, global_id,
                                        nir_undef(&b, 1, 32), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);

   /* We need a SCOPE_DEVICE memory_scope because ACO will avoid
    * creating a vmcnt(0) because it expects the L1 cache to keep memory
    * operations in-order for the same workgroup. The vmcnt(0) seems
    * necessary however. */
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_DEVICE,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_ssbo);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, global_id, nir_undef(&b, 1, 32), data,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);
   return b.shader;
}

nir_shader *
radv_meta_nir_build_dcc_decompress_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "dcc_decompress_compute");

   /* We need at least 16/16/1 to cover an entire DCC block in a single workgroup. */
   b.shader->info.workgroup_size[0] = 16;
   b.shader->info.workgroup_size[1] = 16;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_image, img_type, "in_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);
   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, global_id, 0), nir_channel(&b, global_id, 1), nir_undef(&b, 1, 32),
                                 nir_undef(&b, 1, 32));

   nir_def *data = nir_image_deref_load(&b, 4, 32, &nir_build_deref_var(&b, input_img)->def, img_coord,
                                        nir_undef(&b, 1, 32), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);

   /* We need a SCOPE_DEVICE memory_scope because ACO will avoid
    * creating a vmcnt(0) because it expects the L1 cache to keep memory
    * operations in-order for the same workgroup. The vmcnt(0) seems
    * necessary however. */
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_DEVICE,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_ssbo);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, img_coord, nir_undef(&b, 1, 32), data,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);
   return b.shader;
}

nir_shader *
radv_meta_nir_build_fmask_copy_compute_shader(struct radv_device *dev, int samples)
{
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_MS, false, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_fmask_copy_cs_-%d", samples);

   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_def *wg_id = nir_load_workgroup_id(&b);
   nir_def *block_size = nir_imm_ivec3(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                                       b.shader->info.workgroup_size[2]);

   nir_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   /* Get coordinates. */
   nir_def *src_coord = nir_trim_vector(&b, global_id, 2);
   nir_def *dst_coord = nir_vec4(&b, nir_channel(&b, src_coord, 0), nir_channel(&b, src_coord, 1), nir_undef(&b, 1, 32),
                                 nir_undef(&b, 1, 32));

   nir_tex_src frag_mask_srcs[] = {{
      .src_type = nir_tex_src_coord,
      .src = nir_src_for_ssa(src_coord),
   }};
   nir_def *frag_mask =
      nir_build_tex_deref_instr(&b, nir_texop_fragment_mask_fetch_amd, nir_build_deref_var(&b, input_img), NULL,
                                ARRAY_SIZE(frag_mask_srcs), frag_mask_srcs);

   /* Get the maximum sample used in this fragment. */
   nir_def *max_sample_index = nir_imm_int(&b, 0);
   for (uint32_t s = 0; s < samples; s++) {
      /* max_sample_index = MAX2(max_sample_index, (frag_mask >> (s * 4)) & 0xf) */
      max_sample_index = nir_umax(&b, max_sample_index,
                                  nir_ubitfield_extract(&b, frag_mask, nir_imm_int(&b, 4 * s), nir_imm_int(&b, 4)));
   }

   nir_variable *counter = nir_local_variable_create(b.impl, glsl_int_type(), "counter");
   nir_store_var(&b, counter, nir_imm_int(&b, 0), 0x1);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_def *sample_id = nir_load_var(&b, counter);

      nir_tex_src frag_fetch_srcs[] = {{
                                          .src_type = nir_tex_src_coord,
                                          .src = nir_src_for_ssa(src_coord),
                                       },
                                       {
                                          .src_type = nir_tex_src_ms_index,
                                          .src = nir_src_for_ssa(sample_id),
                                       }};
      nir_def *outval = nir_build_tex_deref_instr(&b, nir_texop_fragment_fetch_amd, nir_build_deref_var(&b, input_img),
                                                  NULL, ARRAY_SIZE(frag_fetch_srcs), frag_fetch_srcs);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, dst_coord, sample_id, outval,
                            nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_MS);

      radv_meta_nir_break_on_count(&b, counter, max_sample_index);
   }
   nir_pop_loop(&b, loop);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_fmask_expand_compute_shader(struct radv_device *device, int samples)
{
   const struct glsl_type *type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, true, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_MS, true, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_nir_init_shader(device, MESA_SHADER_COMPUTE, "meta_fmask_expand_cs-%d", samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;
   output_img->data.access = ACCESS_NON_READABLE;

   nir_deref_instr *input_img_deref = nir_build_deref_var(&b, input_img);
   nir_def *output_img_deref = &nir_build_deref_var(&b, output_img)->def;

   nir_def *tex_coord = radv_meta_nir_get_global_ids(&b, 3);

   nir_def *tex_vals[8];
   for (uint32_t i = 0; i < samples; i++) {
      tex_vals[i] = nir_txf_ms_deref(&b, input_img_deref, tex_coord, nir_imm_int(&b, i));
   }

   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, tex_coord, 0), nir_channel(&b, tex_coord, 1),
                                 nir_channel(&b, tex_coord, 2), nir_undef(&b, 1, 32));

   for (uint32_t i = 0; i < samples; i++) {
      nir_image_deref_store(&b, output_img_deref, img_coord, nir_imm_int(&b, i), tex_vals[i], nir_imm_int(&b, 0),
                            .image_dim = GLSL_SAMPLER_DIM_MS, .image_array = true);
   }

   return b.shader;
}

static nir_def *
radv_meta_build_resolve_srgb_conversion(nir_builder *b, nir_def *input)
{
   unsigned i;
   nir_def *comp[4];
   for (i = 0; i < 3; i++)
      comp[i] = nir_format_linear_to_srgb(b, nir_channel(b, input, i));
   comp[3] = nir_channels(b, input, 1 << 3);
   return nir_vec(b, comp, 4);
}

nir_shader *
radv_meta_nir_build_resolve_compute_shader(struct radv_device *dev, bool is_integer, bool is_srgb, int samples)
{
   enum glsl_base_type img_base_type = is_integer ? GLSL_TYPE_UINT : GLSL_TYPE_FLOAT;
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, img_base_type);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D, false, img_base_type);
   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_resolve_cs-%d-%s", samples,
                                         is_integer ? "int" : (is_srgb ? "srgb" : "float"));
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 2);

   nir_def *src_offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);
   nir_def *dst_offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 8), .range = 16);

   nir_def *src_coord = nir_iadd(&b, global_id, src_offset);
   nir_def *dst_coord = nir_iadd(&b, global_id, dst_offset);

   nir_variable *color = nir_local_variable_create(b.impl, glsl_vec4_type(), "color");

   radv_meta_nir_build_resolve_shader_core(dev, &b, is_integer, samples, input_img, color, src_coord);

   nir_def *outval = nir_load_var(&b, color);
   if (is_srgb)
      outval = radv_meta_build_resolve_srgb_conversion(&b, outval);

   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, dst_coord, 0), nir_channel(&b, dst_coord, 1), nir_undef(&b, 1, 32),
                                 nir_undef(&b, 1, 32));

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, img_coord, nir_undef(&b, 1, 32), outval,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);
   return b.shader;
}

static const char *
get_resolve_mode_str(VkResolveModeFlagBits resolve_mode)
{
   switch (resolve_mode) {
   case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT:
      return "zero";
   case VK_RESOLVE_MODE_AVERAGE_BIT:
      return "average";
   case VK_RESOLVE_MODE_MIN_BIT:
      return "min";
   case VK_RESOLVE_MODE_MAX_BIT:
      return "max";
   default:
      unreachable("invalid resolve mode");
   }
}

nir_shader *
radv_meta_nir_build_depth_stencil_resolve_compute_shader(struct radv_device *dev, int samples,
                                                         enum radv_meta_resolve_type index,
                                                         VkResolveModeFlagBits resolve_mode)
{
   enum glsl_base_type img_base_type = index == RADV_META_DEPTH_RESOLVE ? GLSL_TYPE_FLOAT : GLSL_TYPE_UINT;
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, true, img_base_type);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D, true, img_base_type);

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_COMPUTE, "meta_resolve_cs_%s-%s-%d",
                                         index == RADV_META_DEPTH_RESOLVE ? "depth" : "stencil",
                                         get_resolve_mode_str(resolve_mode), samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = radv_meta_nir_get_global_ids(&b, 3);

   nir_def *offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);

   nir_def *resolve_coord = nir_iadd(&b, nir_trim_vector(&b, global_id, 2), offset);

   nir_def *img_coord =
      nir_vec3(&b, nir_channel(&b, resolve_coord, 0), nir_channel(&b, resolve_coord, 1), nir_channel(&b, global_id, 2));

   nir_deref_instr *input_img_deref = nir_build_deref_var(&b, input_img);
   nir_def *outval = nir_txf_ms_deref(&b, input_img_deref, img_coord, nir_imm_int(&b, 0));

   if (resolve_mode != VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) {
      for (int i = 1; i < samples; i++) {
         nir_def *si = nir_txf_ms_deref(&b, input_img_deref, img_coord, nir_imm_int(&b, i));

         switch (resolve_mode) {
         case VK_RESOLVE_MODE_AVERAGE_BIT:
            assert(index == RADV_META_DEPTH_RESOLVE);
            outval = nir_fadd(&b, outval, si);
            break;
         case VK_RESOLVE_MODE_MIN_BIT:
            if (index == RADV_META_DEPTH_RESOLVE)
               outval = nir_fmin(&b, outval, si);
            else
               outval = nir_umin(&b, outval, si);
            break;
         case VK_RESOLVE_MODE_MAX_BIT:
            if (index == RADV_META_DEPTH_RESOLVE)
               outval = nir_fmax(&b, outval, si);
            else
               outval = nir_umax(&b, outval, si);
            break;
         default:
            unreachable("invalid resolve mode");
         }
      }

      if (resolve_mode == VK_RESOLVE_MODE_AVERAGE_BIT)
         outval = nir_fdiv_imm(&b, outval, samples);
   }

   nir_def *coord = nir_vec4(&b, nir_channel(&b, img_coord, 0), nir_channel(&b, img_coord, 1),
                             nir_channel(&b, img_coord, 2), nir_undef(&b, 1, 32));
   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32), outval,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D, .image_array = true);
   return b.shader;
}

nir_shader *
radv_meta_nir_build_resolve_fragment_shader(struct radv_device *dev, bool is_integer, int samples)
{
   enum glsl_base_type img_base_type = is_integer ? GLSL_TYPE_UINT : GLSL_TYPE_FLOAT;
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, img_base_type);

   nir_builder b =
      radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_resolve_fs-%d-%s", samples, is_integer ? "int" : "float");

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   color_out->data.location = FRAG_RESULT_DATA0;

   nir_def *pos_in = nir_trim_vector(&b, nir_load_frag_coord(&b), 2);
   nir_def *src_offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);

   nir_def *pos_int = nir_f2i32(&b, pos_in);

   nir_def *img_coord = nir_trim_vector(&b, nir_iadd(&b, pos_int, src_offset), 2);
   nir_variable *color = nir_local_variable_create(b.impl, glsl_vec4_type(), "color");

   radv_meta_nir_build_resolve_shader_core(dev, &b, is_integer, samples, input_img, color, img_coord);

   nir_def *outval = nir_load_var(&b, color);
   nir_store_var(&b, color_out, outval, 0xf);
   return b.shader;
}

nir_shader *
radv_meta_nir_build_depth_stencil_resolve_fragment_shader(struct radv_device *dev, int samples,
                                                          enum radv_meta_resolve_type index,
                                                          VkResolveModeFlagBits resolve_mode)
{
   enum glsl_base_type img_base_type = index == RADV_META_DEPTH_RESOLVE ? GLSL_TYPE_FLOAT : GLSL_TYPE_UINT;
   const struct glsl_type *vec4 = glsl_vec4_type();
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, img_base_type);

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_resolve_fs_%s-%s-%d",
                                         index == RADV_META_DEPTH_RESOLVE ? "depth" : "stencil",
                                         get_resolve_mode_str(resolve_mode), samples);

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *fs_out = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_out");
   fs_out->data.location = index == RADV_META_DEPTH_RESOLVE ? FRAG_RESULT_DEPTH : FRAG_RESULT_STENCIL;

   nir_def *pos_in = nir_trim_vector(&b, nir_load_frag_coord(&b), 2);

   nir_def *pos_int = nir_f2i32(&b, pos_in);

   nir_def *img_coord = nir_trim_vector(&b, pos_int, 2);

   nir_deref_instr *input_img_deref = nir_build_deref_var(&b, input_img);
   nir_def *outval = nir_txf_ms_deref(&b, input_img_deref, img_coord, nir_imm_int(&b, 0));

   if (resolve_mode != VK_RESOLVE_MODE_SAMPLE_ZERO_BIT) {
      for (int i = 1; i < samples; i++) {
         nir_def *si = nir_txf_ms_deref(&b, input_img_deref, img_coord, nir_imm_int(&b, i));

         switch (resolve_mode) {
         case VK_RESOLVE_MODE_AVERAGE_BIT:
            assert(index == RADV_META_DEPTH_RESOLVE);
            outval = nir_fadd(&b, outval, si);
            break;
         case VK_RESOLVE_MODE_MIN_BIT:
            if (index == RADV_META_DEPTH_RESOLVE)
               outval = nir_fmin(&b, outval, si);
            else
               outval = nir_umin(&b, outval, si);
            break;
         case VK_RESOLVE_MODE_MAX_BIT:
            if (index == RADV_META_DEPTH_RESOLVE)
               outval = nir_fmax(&b, outval, si);
            else
               outval = nir_umax(&b, outval, si);
            break;
         default:
            unreachable("invalid resolve mode");
         }
      }

      if (resolve_mode == VK_RESOLVE_MODE_AVERAGE_BIT)
         outval = nir_fdiv_imm(&b, outval, samples);
   }

   nir_store_var(&b, fs_out, outval, 0x1);

   return b.shader;
}

nir_shader *
radv_meta_nir_build_resolve_fs(struct radv_device *dev)
{
   const struct glsl_type *vec4 = glsl_vec4_type();
   nir_variable *f_color;

   nir_builder b = radv_meta_nir_init_shader(dev, MESA_SHADER_FRAGMENT, "meta_resolve_fs");

   f_color = nir_variable_create(b.shader, nir_var_shader_out, vec4, "f_color");
   f_color->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, f_color, nir_imm_vec4(&b, 0.0, 0.0, 0.0, 1.0), 0xf);

   return b.shader;
}
