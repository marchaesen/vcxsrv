/*
 * Copyright 2025 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "vl_deint_filter_cs.h"
#include "vl_video_buffer.h"
#include "pipe/p_context.h"
#include "nir/nir_builder.h"

static inline nir_def *
texture(nir_builder *b, nir_def *pos, nir_variable *sampler)
{
   nir_deref_instr *tex_deref = nir_build_deref_var(b, sampler);
   return nir_tex_deref(b, tex_deref, tex_deref, nir_channels(b, pos, 0x3));
}

static inline void
image_store(nir_builder *b, nir_def *pos, nir_def *color, nir_variable *image)
{
   nir_def *zero = nir_imm_int(b, 0);
   nir_def *undef32 = nir_undef(b, 1, 32);
   pos = nir_pad_vector_imm_int(b, pos, 0, 4);
   nir_image_deref_store(b, &nir_build_deref_var(b, image)->def, pos, undef32, color, zero);
}

static void *
create_deint_shader(struct vl_deint_filter *filter, unsigned field)
{
   const struct glsl_type *sampler_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_RECT, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *image_type =
      glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT);
   const nir_shader_compiler_options *options =
      filter->pipe->screen->get_compiler_options(filter->pipe->screen, PIPE_SHADER_IR_NIR, PIPE_SHADER_COMPUTE);

   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, options, "vl:deint");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   nir_variable *samplers[4];
   for (unsigned i = 0; i < 4; i++) {
      samplers[i] = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "sampler");
      samplers[i]->data.binding = i;
      BITSET_SET(b.shader->info.textures_used, i);
      BITSET_SET(b.shader->info.samplers_used, i);
   }

   nir_variable *sampler_prevprev = samplers[0];
   nir_variable *sampler_prev = samplers[1];
   nir_variable *sampler_cur = samplers[2];
   nir_variable *sampler_next = samplers[3];

   nir_variable *image = nir_variable_create(b.shader, nir_var_image, image_type, "image");
   image->data.binding = 0;
   BITSET_SET(b.shader->info.images_used, 0);

   nir_def *block_ids = nir_load_workgroup_id(&b);
   nir_def *local_ids = nir_load_local_invocation_id(&b);
   nir_def *ipos = nir_iadd(&b, nir_imul(&b, block_ids, nir_imm_ivec3(&b, 8, 8, 1)), local_ids);

   nir_def *curr_field = nir_imod_imm(&b, nir_channel(&b, ipos, 1), 2);
   nir_if *if_curr_field = nir_push_if(&b, nir_ieq_imm(&b, curr_field, field)); {
      /* Blit current field */
      nir_def *pos = nir_u2f32(&b, ipos);
      pos = nir_fadd_imm(&b, pos, 0.5f);
      nir_def *color = texture(&b, pos, sampler_cur);
      image_store(&b, ipos, color, image);
   } nir_push_else(&b, if_curr_field); {
      /* Interpolate other field */
      nir_def *diffx, *diffy, *weave, *linear;
      nir_def *pos = nir_u2f32(&b, ipos);
      nir_def *top = nir_fadd(&b, pos, nir_imm_vec2(&b, 0.0f, field ? 0.5f : -0.5f));
      nir_def *bot = nir_fadd(&b, pos, nir_imm_vec2(&b, 0.0f, field ? 1.5f : 0.5f));

      if (field) {
         /* Interpolating top field -> current field is a bottom field */
         /* cur vs prev2 */
         nir_def *ta = texture(&b, bot, sampler_cur);
         nir_def *tb = texture(&b, bot, sampler_prevprev);
         diffx = nir_fadd(&b, ta, nir_fneg(&b, tb));

         /* prev vs next */
         ta = texture(&b, top, sampler_prev);
         tb = texture(&b, top, sampler_next);
         diffy = nir_fadd(&b, ta, nir_fneg(&b, tb));

         /* Weave with prev top field */
         top = nir_fadd(&b, top, nir_imm_vec2(&b, 0.5f, 0.0f));
         weave = texture(&b, top, sampler_prev);
         /* Get linear interpolation from current bottom field */
         bot = nir_fadd(&b, bot, nir_imm_vec2(&b, 0.5f, 0.0f));
         linear = texture(&b, bot, sampler_cur);
      } else {
         /* Interpolating bottom field -> current field is a top field */
         /* cur vs prev2 */
         nir_def *ta = texture(&b, top, sampler_cur);
         nir_def *tb = texture(&b, top, sampler_prevprev);
         diffx = nir_fadd(&b, ta, nir_fneg(&b, tb));

         /* prev vs next */
         ta = texture(&b, bot, sampler_prev);
         tb = texture(&b, bot, sampler_next);
         diffy = nir_fadd(&b, ta, nir_fneg(&b, tb));

         /* Weave with prev bottom field */
         bot = nir_fadd(&b, bot, nir_imm_vec2(&b, 0.5f, 0.0f));
         weave = texture(&b, bot, sampler_prev);
         /* Get linear interpolation from current top field */
         top = nir_fadd(&b, top, nir_imm_vec2(&b, 0.5f, 0.0f));
         linear = texture(&b, top, sampler_cur);
      }

      /* Absolute maximum of differences */
      nir_def *diff = nir_fmax(&b, nir_fabs(&b, diffx), nir_fabs(&b, diffy));

      /* Mix between weave and linear
       * fully weave if diff < 6 (0.02353), fully interpolate if diff > 14 (0.05490)
       */
      diff = nir_fadd_imm(&b, diff, -0.02353f);
      diff = nir_fmul_imm(&b, diff, 31.8750f);
      nir_def *color = nir_flrp(&b, weave, linear, nir_fsat(&b, diff));
      image_store(&b, ipos, color, image);
   }
   nir_pop_if(&b, if_curr_field);

   filter->pipe->screen->finalize_nir(filter->pipe->screen, b.shader);

   struct pipe_compute_state state = {
      .ir_type = PIPE_SHADER_IR_NIR,
      .prog = b.shader,
   };
   return filter->pipe->create_compute_state(filter->pipe, &state);
}

bool
vl_deint_filter_cs_init(struct vl_deint_filter *filter)
{
   if (!filter->interleaved)
      return false;

   struct pipe_video_buffer templ = {
      .buffer_format = PIPE_FORMAT_NV12,
      .width = filter->video_width,
      .height = filter->video_height,
   };
   filter->video_buffer = vl_video_buffer_create(filter->pipe, &templ);
   if (!filter->video_buffer)
      goto error;

   struct pipe_sampler_state sampler = {
      .wrap_s = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
      .wrap_t = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
      .wrap_r = PIPE_TEX_WRAP_CLAMP_TO_EDGE,
      .min_img_filter = PIPE_TEX_FILTER_LINEAR,
      .min_mip_filter = PIPE_TEX_MIPFILTER_NONE,
      .mag_img_filter = PIPE_TEX_FILTER_LINEAR,
   };
   filter->sampler[0] = filter->pipe->create_sampler_state(filter->pipe, &sampler);
   filter->sampler[1] = filter->sampler[2] = filter->sampler[3] = filter->sampler[0];
   if (!filter->sampler[0])
      goto error;

   filter->cs_deint_top = create_deint_shader(filter, 0);
   if (!filter->cs_deint_top)
      goto error;

   filter->cs_deint_bottom = create_deint_shader(filter, 1);
   if (!filter->cs_deint_bottom)
      goto error;

   return true;

error:
   vl_deint_filter_cs_cleanup(filter);
   return false;
}

void
vl_deint_filter_cs_cleanup(struct vl_deint_filter *filter)
{
   if (filter->video_buffer)
      filter->video_buffer->destroy(filter->video_buffer);
   if (filter->sampler[0])
      filter->pipe->delete_sampler_state(filter->pipe, filter->sampler[0]);
   if (filter->cs_deint_top)
      filter->pipe->delete_compute_state(filter->pipe, filter->cs_deint_top);
   if (filter->cs_deint_bottom)
      filter->pipe->delete_compute_state(filter->pipe, filter->cs_deint_bottom);
}

void
vl_deint_filter_cs_render(struct vl_deint_filter *filter,
                          struct pipe_video_buffer *prevprev,
                          struct pipe_video_buffer *prev,
                          struct pipe_video_buffer *cur,
                          struct pipe_video_buffer *next,
                          unsigned field)
{
   struct pipe_sampler_view **cur_sv;
   struct pipe_sampler_view **prevprev_sv;
   struct pipe_sampler_view **prev_sv;
   struct pipe_sampler_view **next_sv;
   struct pipe_sampler_view *sampler_views[4];
   struct pipe_surface **dst_surfaces;

   /* Set up destination and source */
   dst_surfaces = filter->video_buffer->get_surfaces(filter->video_buffer);
   cur_sv = cur->get_sampler_view_planes(cur);
   prevprev_sv = prevprev->get_sampler_view_planes(prevprev);
   prev_sv = prev->get_sampler_view_planes(prev);
   next_sv = next->get_sampler_view_planes(next);

   filter->pipe->bind_sampler_states(filter->pipe, PIPE_SHADER_COMPUTE,
                                     0, 4, filter->sampler);

   for (unsigned i = 0; i < 2; i++) {
      struct pipe_surface *dst = dst_surfaces[i];

      /* Update sampler view sources  */
      sampler_views[0] = prevprev_sv[i];
      sampler_views[1] = prev_sv[i];
      sampler_views[2] = cur_sv[i];
      sampler_views[3] = next_sv[i];
      filter->pipe->set_sampler_views(filter->pipe, PIPE_SHADER_COMPUTE,
                                      0, 4, 0, false, sampler_views);

      /* Bind the image */
      struct pipe_image_view image = {
         .resource = dst->texture,
         .access = PIPE_IMAGE_ACCESS_WRITE,
         .shader_access = PIPE_IMAGE_ACCESS_WRITE,
         .format = dst->texture->format,
      };
      filter->pipe->set_shader_images(filter->pipe, PIPE_SHADER_COMPUTE,
                                      0, 1, 0, &image);

      /* Bind compute shader */
      filter->pipe->bind_compute_state(filter->pipe,
                                       field ? filter->cs_deint_bottom :
                                       filter->cs_deint_top);

      /* Dispatch compute */
      struct pipe_grid_info info = {
         .block[0] = 8,
         .last_block[0] = dst->texture->width0 % info.block[0],
         .block[1] = 8,
         .last_block[1] = dst->texture->height0 % info.block[1],
         .block[2] = 1,
         .grid[0] = DIV_ROUND_UP(dst->texture->width0, info.block[0]),
         .grid[1] = DIV_ROUND_UP(dst->texture->height0, info.block[1]),
         .grid[2] = 1,
      };
      filter->pipe->launch_grid(filter->pipe, &info);

      filter->pipe->memory_barrier(filter->pipe, PIPE_BARRIER_ALL);
   }
}
