/**************************************************************************
 *
 * Copyright 2014 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_tests.h"

#include "util/u_draw_quad.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_simple_shaders.h"
#include "util/u_surface.h"
#include "util/u_string.h"
#include "util/u_tile.h"
#include "tgsi/tgsi_strings.h"
#include "tgsi/tgsi_text.h"
#include "cso_cache/cso_context.h"
#include <stdio.h>

#define TOLERANCE 0.01

static struct pipe_resource *
util_create_texture2d(struct pipe_screen *screen, unsigned width,
                      unsigned height, enum pipe_format format)
{
   struct pipe_resource templ = {{0}};

   templ.target = PIPE_TEXTURE_2D;
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.format = format;
   templ.usage = PIPE_USAGE_DEFAULT;
   templ.bind = PIPE_BIND_SAMPLER_VIEW |
                (util_format_is_depth_or_stencil(format) ?
                    PIPE_BIND_DEPTH_STENCIL : PIPE_BIND_RENDER_TARGET);

   return screen->resource_create(screen, &templ);
}

static void
util_set_framebuffer_cb0(struct cso_context *cso, struct pipe_context *ctx,
			 struct pipe_resource *tex)
{
   struct pipe_surface templ = {{0}}, *surf;
   struct pipe_framebuffer_state fb = {0};

   templ.format = tex->format;
   surf = ctx->create_surface(ctx, tex, &templ);

   fb.width = tex->width0;
   fb.height = tex->height0;
   fb.cbufs[0] = surf;
   fb.nr_cbufs = 1;

   cso_set_framebuffer(cso, &fb);
   pipe_surface_reference(&surf, NULL);
}

static void
util_set_blend_normal(struct cso_context *cso)
{
   struct pipe_blend_state blend = {0};

   blend.rt[0].colormask = PIPE_MASK_RGBA;
   cso_set_blend(cso, &blend);
}

static void
util_set_dsa_disable(struct cso_context *cso)
{
   struct pipe_depth_stencil_alpha_state dsa = {{0}};

   cso_set_depth_stencil_alpha(cso, &dsa);
}

static void
util_set_rasterizer_normal(struct cso_context *cso)
{
   struct pipe_rasterizer_state rs = {0};

   rs.half_pixel_center = 1;
   rs.bottom_edge_rule = 1;
   rs.depth_clip = 1;

   cso_set_rasterizer(cso, &rs);
}

static void
util_set_max_viewport(struct cso_context *cso, struct pipe_resource *tex)
{
   struct pipe_viewport_state viewport;

   viewport.scale[0] = 0.5f * tex->width0;
   viewport.scale[1] = 0.5f * tex->height0;
   viewport.scale[2] = 1.0f;
   viewport.translate[0] = 0.5f * tex->width0;
   viewport.translate[1] = 0.5f * tex->height0;
   viewport.translate[2] = 0.0f;

   cso_set_viewport(cso, &viewport);
}

static void
util_set_interleaved_vertex_elements(struct cso_context *cso,
                                     unsigned num_elements)
{
   unsigned i;
   struct pipe_vertex_element *velem =
      calloc(1, num_elements * sizeof(struct pipe_vertex_element));

   for (i = 0; i < num_elements; i++) {
      velem[i].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
      velem[i].src_offset = i * 16;
   }

   cso_set_vertex_elements(cso, num_elements, velem);
   free(velem);
}

static void *
util_set_passthrough_vertex_shader(struct cso_context *cso,
                                   struct pipe_context *ctx,
                                   bool window_space)
{
   static const uint vs_attribs[] = {
      TGSI_SEMANTIC_POSITION,
      TGSI_SEMANTIC_GENERIC
   };
   static const uint vs_indices[] = {0, 0};
   void *vs;

   vs = util_make_vertex_passthrough_shader(ctx, 2, vs_attribs, vs_indices,
                                            window_space);
   cso_set_vertex_shader_handle(cso, vs);
   return vs;
}

static void
util_set_common_states_and_clear(struct cso_context *cso, struct pipe_context *ctx,
                                 struct pipe_resource *cb)
{
   static const float clear_color[] = {0.1, 0.1, 0.1, 0.1};

   util_set_framebuffer_cb0(cso, ctx, cb);
   util_set_blend_normal(cso);
   util_set_dsa_disable(cso);
   util_set_rasterizer_normal(cso);
   util_set_max_viewport(cso, cb);

   ctx->clear(ctx, PIPE_CLEAR_COLOR0, (void*)clear_color, 0, 0);
}

static void
util_draw_fullscreen_quad(struct cso_context *cso)
{
   static float vertices[] = {
     -1, -1, 0, 1,   0, 0, 0, 0,
     -1,  1, 0, 1,   0, 1, 0, 0,
      1,  1, 0, 1,   1, 1, 0, 0,
      1, -1, 0, 1,   1, 0, 0, 0
   };
   util_set_interleaved_vertex_elements(cso, 2);
   util_draw_user_vertex_buffer(cso, vertices, PIPE_PRIM_QUADS, 4, 2);
}

/**
 * Probe and test if the rectangle contains the expected color.
 *
 * If "num_expected_colors" > 1, at least one expected color must match
 * the probed color. "expected" should be an array of 4*num_expected_colors
 * floats.
 */
static bool
util_probe_rect_rgba_multi(struct pipe_context *ctx, struct pipe_resource *tex,
                           unsigned offx, unsigned offy, unsigned w,
                           unsigned h,
                           const float *expected,
                           unsigned num_expected_colors)
{
   struct pipe_transfer *transfer;
   void *map;
   float *pixels = malloc(w * h * 4 * sizeof(float));
   unsigned x,y,e,c;
   bool pass = true;

   map = pipe_transfer_map(ctx, tex, 0, 0, PIPE_TRANSFER_READ,
                           offx, offy, w, h, &transfer);
   pipe_get_tile_rgba(transfer, map, 0, 0, w, h, pixels);
   pipe_transfer_unmap(ctx, transfer);

   for (e = 0; e < num_expected_colors; e++) {
      for (y = 0; y < h; y++) {
         for (x = 0; x < w; x++) {
            float *probe = &pixels[(y*w + x)*4];

            for (c = 0; c < 4; c++) {
               if (fabs(probe[c] - expected[e*4+c]) >= TOLERANCE) {
                  if (e < num_expected_colors-1)
                     goto next_color; /* test the next expected color */

                  printf("Probe color at (%i,%i),  ", offx+x, offy+y);
                  printf("Expected: %.3f, %.3f, %.3f, %.3f,  ",
                         expected[e*4], expected[e*4+1],
                         expected[e*4+2], expected[e*4+3]);
                  printf("Got: %.3f, %.3f, %.3f, %.3f\n",
                         probe[0], probe[1], probe[2], probe[2]);
                  pass = false;
                  goto done;
               }
            }
         }
      }
      break; /* this color was successful */

   next_color:;
   }
done:

   free(pixels);
   return pass;
}

static bool
util_probe_rect_rgba(struct pipe_context *ctx, struct pipe_resource *tex,
                     unsigned offx, unsigned offy, unsigned w, unsigned h,
                     const float *expected)
{
   return util_probe_rect_rgba_multi(ctx, tex, offx, offy, w, h, expected, 1);
}

enum {
   SKIP = -1,
   FAIL = 0, /* also "false" */
   PASS = 1 /* also "true" */
};

static void
util_report_result_helper(int status, const char *name, ...)
{
   char buf[256];
   va_list ap;

   va_start(ap, name);
   util_vsnprintf(buf, sizeof(buf), name, ap);
   va_end(ap);

   printf("Test(%s) = %s\n", buf,
          status == SKIP ? "skip" :
          status == PASS ? "pass" : "fail");
}

#define util_report_result(status) util_report_result_helper(status, __func__)

/**
 * Test TGSI_PROPERTY_VS_WINDOW_SPACE_POSITION.
 *
 * The viewport state is set as usual, but it should have no effect.
 * Clipping should also be disabled.
 *
 * POSITION.xyz should already be multiplied by 1/w and POSITION.w should
 * contain 1/w. By setting w=0, we can test that POSITION.xyz isn't
 * multiplied by 1/w (otherwise nothing would be rendered).
 *
 * TODO: Whether the value of POSITION.w is correctly interpreted as 1/w
 *       during perspective interpolation is not tested.
 */
static void
tgsi_vs_window_space_position(struct pipe_context *ctx)
{
   struct cso_context *cso;
   struct pipe_resource *cb;
   void *fs, *vs;
   bool pass = true;
   static const float red[] = {1, 0, 0, 1};

   if (!ctx->screen->get_param(ctx->screen,
                               PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION)) {
      util_report_result(SKIP);
      return;
   }

   cso = cso_create_context(ctx);
   cb = util_create_texture2d(ctx->screen, 256, 256,
                              PIPE_FORMAT_R8G8B8A8_UNORM);
   util_set_common_states_and_clear(cso, ctx, cb);

   /* Fragment shader. */
   fs = util_make_fragment_passthrough_shader(ctx, TGSI_SEMANTIC_GENERIC,
                                       TGSI_INTERPOLATE_LINEAR, TRUE);
   cso_set_fragment_shader_handle(cso, fs);

   /* Vertex shader. */
   vs = util_set_passthrough_vertex_shader(cso, ctx, true);

   /* Draw. */
   {
      static float vertices[] = {
          0,   0, 0, 0,   1,  0, 0, 1,
          0, 256, 0, 0,   1,  0, 0, 1,
        256, 256, 0, 0,   1,  0, 0, 1,
        256,   0, 0, 0,   1,  0, 0, 1,
      };
      util_set_interleaved_vertex_elements(cso, 2);
      util_draw_user_vertex_buffer(cso, vertices, PIPE_PRIM_QUADS, 4, 2);
   }

   /* Probe pixels. */
   pass = pass && util_probe_rect_rgba(ctx, cb, 0, 0,
                                       cb->width0, cb->height0, red);

   /* Cleanup. */
   cso_destroy_context(cso);
   ctx->delete_vs_state(ctx, vs);
   ctx->delete_fs_state(ctx, fs);
   pipe_resource_reference(&cb, NULL);

   util_report_result(pass);
}

static void
null_sampler_view(struct pipe_context *ctx, unsigned tgsi_tex_target)
{
   struct cso_context *cso;
   struct pipe_resource *cb;
   void *fs, *vs;
   bool pass = true;
   /* 2 expected colors: */
   static const float expected_tex[] = {0, 0, 0, 1,
                                        0, 0, 0, 0};
   static const float expected_buf[] = {0, 0, 0, 0};
   const float *expected = tgsi_tex_target == TGSI_TEXTURE_BUFFER ?
                              expected_buf : expected_tex;
   unsigned num_expected = tgsi_tex_target == TGSI_TEXTURE_BUFFER ? 1 : 2;

   if (tgsi_tex_target == TGSI_TEXTURE_BUFFER &&
       !ctx->screen->get_param(ctx->screen, PIPE_CAP_TEXTURE_BUFFER_OBJECTS)) {
      util_report_result_helper(SKIP, "%s: %s", __func__,
                                tgsi_texture_names[tgsi_tex_target]);
      return;
   }

   cso = cso_create_context(ctx);
   cb = util_create_texture2d(ctx->screen, 256, 256,
                              PIPE_FORMAT_R8G8B8A8_UNORM);
   util_set_common_states_and_clear(cso, ctx, cb);

   ctx->set_sampler_views(ctx, PIPE_SHADER_FRAGMENT, 0, 1, NULL);

   /* Fragment shader. */
   fs = util_make_fragment_tex_shader(ctx, tgsi_tex_target,
                                      TGSI_INTERPOLATE_LINEAR,
                                      TGSI_RETURN_TYPE_FLOAT);
   cso_set_fragment_shader_handle(cso, fs);

   /* Vertex shader. */
   vs = util_set_passthrough_vertex_shader(cso, ctx, false);
   util_draw_fullscreen_quad(cso);

   /* Probe pixels. */
   pass = pass && util_probe_rect_rgba_multi(ctx, cb, 0, 0,
                                  cb->width0, cb->height0, expected,
                                  num_expected);

   /* Cleanup. */
   cso_destroy_context(cso);
   ctx->delete_vs_state(ctx, vs);
   ctx->delete_fs_state(ctx, fs);
   pipe_resource_reference(&cb, NULL);

   util_report_result_helper(pass, "%s: %s", __func__,
                             tgsi_texture_names[tgsi_tex_target]);
}

static void
null_constant_buffer(struct pipe_context *ctx)
{
   struct cso_context *cso;
   struct pipe_resource *cb;
   void *fs, *vs;
   bool pass = true;
   static const float zero[] = {0, 0, 0, 0};

   cso = cso_create_context(ctx);
   cb = util_create_texture2d(ctx->screen, 256, 256,
                              PIPE_FORMAT_R8G8B8A8_UNORM);
   util_set_common_states_and_clear(cso, ctx, cb);

   ctx->set_constant_buffer(ctx, PIPE_SHADER_FRAGMENT, 0, NULL);

   /* Fragment shader. */
   {
      static const char *text = /* I don't like ureg... */
            "FRAG\n"
            "DCL CONST[0]\n"
            "DCL OUT[0], COLOR\n"

            "MOV OUT[0], CONST[0]\n"
            "END\n";
      struct tgsi_token tokens[1000];
      struct pipe_shader_state state;

      if (!tgsi_text_translate(text, tokens, ARRAY_SIZE(tokens))) {
         puts("Can't compile a fragment shader.");
         util_report_result(FAIL);
         return;
      }
      pipe_shader_state_from_tgsi(&state, tokens);
      fs = ctx->create_fs_state(ctx, &state);
      cso_set_fragment_shader_handle(cso, fs);
   }

   /* Vertex shader. */
   vs = util_set_passthrough_vertex_shader(cso, ctx, false);
   util_draw_fullscreen_quad(cso);

   /* Probe pixels. */
   pass = pass && util_probe_rect_rgba(ctx, cb, 0, 0, cb->width0,
                                       cb->height0, zero);

   /* Cleanup. */
   cso_destroy_context(cso);
   ctx->delete_vs_state(ctx, vs);
   ctx->delete_fs_state(ctx, fs);
   pipe_resource_reference(&cb, NULL);

   util_report_result(pass);
}

static void
null_fragment_shader(struct pipe_context *ctx)
{
   struct cso_context *cso;
   struct pipe_resource *cb;
   void *vs;
   struct pipe_rasterizer_state rs = {0};
   struct pipe_query *query;
   union pipe_query_result qresult;

   cso = cso_create_context(ctx);
   cb = util_create_texture2d(ctx->screen, 256, 256,
                              PIPE_FORMAT_R8G8B8A8_UNORM);
   util_set_common_states_and_clear(cso, ctx, cb);

   /* No rasterization. */
   rs.rasterizer_discard = 1;
   cso_set_rasterizer(cso, &rs);

   vs = util_set_passthrough_vertex_shader(cso, ctx, false);

   query = ctx->create_query(ctx, PIPE_QUERY_PRIMITIVES_GENERATED, 0);
   ctx->begin_query(ctx, query);
   util_draw_fullscreen_quad(cso);
   ctx->end_query(ctx, query);
   ctx->get_query_result(ctx, query, true, &qresult);

   /* Cleanup. */
   cso_destroy_context(cso);
   ctx->delete_vs_state(ctx, vs);
   ctx->destroy_query(ctx, query);
   pipe_resource_reference(&cb, NULL);

   /* Check PRIMITIVES_GENERATED. */
   util_report_result(qresult.u64 == 2);
}

/**
 * Run all tests. This should be run with a clean context after
 * context_create.
 */
void
util_run_tests(struct pipe_screen *screen)
{
   struct pipe_context *ctx = screen->context_create(screen, NULL, 0);

   null_fragment_shader(ctx);
   tgsi_vs_window_space_position(ctx);
   null_sampler_view(ctx, TGSI_TEXTURE_2D);
   null_sampler_view(ctx, TGSI_TEXTURE_BUFFER);
   null_constant_buffer(ctx);

   ctx->destroy(ctx);

   puts("Done. Exiting..");
   exit(0);
}
