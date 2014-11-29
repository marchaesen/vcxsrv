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
#include "util/u_simple_shaders.h"
#include "util/u_surface.h"
#include "util/u_tile.h"
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
   int i;
   struct pipe_vertex_element *velem =
      calloc(1, num_elements * sizeof(struct pipe_vertex_element));

   for (i = 0; i < num_elements; i++) {
      velem[i].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
      velem[i].src_offset = i * 16;
   }

   cso_set_vertex_elements(cso, num_elements, velem);
   free(velem);
}

static bool
util_probe_rect_rgba(struct pipe_context *ctx, struct pipe_resource *tex,
                     unsigned offx, unsigned offy, unsigned w, unsigned h,
                     const float *expected)
{
   struct pipe_transfer *transfer;
   void *map;
   float *pixels = malloc(w * h * 4 * sizeof(float));
   int x,y,c;
   bool pass = true;

   map = pipe_transfer_map(ctx, tex, 0, 0, PIPE_TRANSFER_READ,
                           offx, offy, w, h, &transfer);
   pipe_get_tile_rgba(transfer, map, 0, 0, w, h, pixels);
   pipe_transfer_unmap(ctx, transfer);

   for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
         float *probe = &pixels[(y*w + x)*4];

         for (c = 0; c < 4; c++)
            if (fabs(probe[c] - expected[c]) >= TOLERANCE) {
               printf("Probe color at (%i,%i),  ", offx+x, offy+y);
               printf("Expected: %.3f, %.3f, %.3f, %.3f,  ",
                      expected[0], expected[1], expected[2], expected[3]);
               printf("Got: %.3f, %.3f, %.3f, %.3f\n",
                      probe[0], probe[1], probe[2], probe[2]);
               pass = false;
               goto done;
            }
      }
   }
done:

   free(pixels);
   return pass;
}

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

   static uint vs_attribs[] = {
      TGSI_SEMANTIC_POSITION,
      TGSI_SEMANTIC_GENERIC
   };
   static uint vs_indices[] = {0, 0};
   static float vertices[] = {
       0,   0, 0, 0,   1,  0, 0, 1,
       0, 256, 0, 0,   1,  0, 0, 1,
     256, 256, 0, 0,   1,  0, 0, 1,
     256,   0, 0, 0,   1,  0, 0, 1,
   };
   static float red[] = {1, 0, 0, 1};
   static float clear_color[] = {0.1, 0.1, 0.1, 0.1};

   if (!ctx->screen->get_param(ctx->screen,
                               PIPE_CAP_TGSI_VS_WINDOW_SPACE_POSITION)) {
      printf("Test(%s) = skip\n", __func__);
      return;
   }

   cso = cso_create_context(ctx);
   cb = util_create_texture2d(ctx->screen, 256, 256,
                              PIPE_FORMAT_R8G8B8A8_UNORM);

   /* Set states. */
   util_set_framebuffer_cb0(cso, ctx, cb);
   util_set_blend_normal(cso);
   util_set_dsa_disable(cso);
   util_set_rasterizer_normal(cso);
   util_set_max_viewport(cso, cb);
   util_set_interleaved_vertex_elements(cso, 2);

   /* Fragment shader. */
   fs = util_make_fragment_passthrough_shader(ctx, TGSI_SEMANTIC_GENERIC,
                                       TGSI_INTERPOLATE_LINEAR, TRUE);
   cso_set_fragment_shader_handle(cso, fs);

   /* Vertex shader. */
   vs = util_make_vertex_passthrough_shader(ctx, 2, vs_attribs, vs_indices,
                                            TRUE);
   cso_set_vertex_shader_handle(cso, vs);

   /* Clear and draw. */
   ctx->clear(ctx, PIPE_CLEAR_COLOR0, (void*)clear_color, 0, 0);
   util_draw_user_vertex_buffer(cso, vertices, PIPE_PRIM_QUADS, 4, 2);

   /* Probe pixels. */
   pass = pass && util_probe_rect_rgba(ctx, cb, 0, 0,
                                       cb->width0, cb->height0, red);

   /* Cleanup. */
   cso_release_all(cso);
   cso_destroy_context(cso);
   ctx->delete_vs_state(ctx, vs);
   ctx->delete_fs_state(ctx, fs);
   pipe_resource_reference(&cb, NULL);

   printf("Test(%s) = %s\n", __func__, pass ? "pass" : "fail");
}

/**
 * Run all tests. This should be run with a clean context after
 * context_create.
 */
void
util_run_tests(struct pipe_context *ctx)
{
   tgsi_vs_window_space_position(ctx);
}
