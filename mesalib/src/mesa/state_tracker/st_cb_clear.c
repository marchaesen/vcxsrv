/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
 * All Rights Reserved.
 * Copyright 2009 VMware, Inc.  All Rights Reserved.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  *   Brian Paul
  *   Michel Dänzer
  */

#include "main/glheader.h"
#include "main/accum.h"
#include "main/formats.h"
#include "main/macros.h"
#include "main/glformats.h"
#include "program/prog_instruction.h"
#include "st_context.h"
#include "st_atom.h"
#include "st_cb_bitmap.h"
#include "st_cb_clear.h"
#include "st_cb_fbo.h"
#include "st_draw.h"
#include "st_format.h"
#include "st_program.h"

#include "pipe/p_context.h"
#include "pipe/p_shader_tokens.h"
#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include "util/u_format.h"
#include "util/u_framebuffer.h"
#include "util/u_inlines.h"
#include "util/u_simple_shaders.h"

#include "cso_cache/cso_context.h"


/**
 * Do per-context initialization for glClear.
 */
void
st_init_clear(struct st_context *st)
{
   memset(&st->clear, 0, sizeof(st->clear));

   st->clear.raster.half_pixel_center = 1;
   st->clear.raster.bottom_edge_rule = 1;
   st->clear.raster.depth_clip = 1;
}


/**
 * Free per-context state for glClear.
 */
void
st_destroy_clear(struct st_context *st)
{
   if (st->clear.fs) {
      cso_delete_fragment_shader(st->cso_context, st->clear.fs);
      st->clear.fs = NULL;
   }
   if (st->clear.vs) {
      cso_delete_vertex_shader(st->cso_context, st->clear.vs);
      st->clear.vs = NULL;
   }
   if (st->clear.vs_layered) {
      cso_delete_vertex_shader(st->cso_context, st->clear.vs_layered);
      st->clear.vs_layered = NULL;
   }
   if (st->clear.gs_layered) {
      cso_delete_geometry_shader(st->cso_context, st->clear.gs_layered);
      st->clear.gs_layered = NULL;
   }
}


/**
 * Helper function to set the fragment shaders.
 */
static inline void
set_fragment_shader(struct st_context *st)
{
   if (!st->clear.fs)
      st->clear.fs =
         util_make_fragment_passthrough_shader(st->pipe, TGSI_SEMANTIC_GENERIC,
                                               TGSI_INTERPOLATE_CONSTANT,
                                               TRUE);

   cso_set_fragment_shader_handle(st->cso_context, st->clear.fs);
}


/**
 * Helper function to set the vertex shader.
 */
static inline void
set_vertex_shader(struct st_context *st)
{
   /* vertex shader - still required to provide the linkage between
    * fragment shader input semantics and vertex_element/buffers.
    */
   if (!st->clear.vs)
   {
      const uint semantic_names[] = { TGSI_SEMANTIC_POSITION,
                                      TGSI_SEMANTIC_GENERIC };
      const uint semantic_indexes[] = { 0, 0 };
      st->clear.vs = util_make_vertex_passthrough_shader(st->pipe, 2,
                                                         semantic_names,
                                                         semantic_indexes,
                                                         FALSE);
   }

   cso_set_vertex_shader_handle(st->cso_context, st->clear.vs);
   cso_set_geometry_shader_handle(st->cso_context, NULL);
}


static void
set_vertex_shader_layered(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;

   if (!pipe->screen->get_param(pipe->screen, PIPE_CAP_TGSI_INSTANCEID)) {
      assert(!"Got layered clear, but VS instancing is unsupported");
      set_vertex_shader(st);
      return;
   }

   if (!st->clear.vs_layered) {
      bool vs_layer =
         pipe->screen->get_param(pipe->screen, PIPE_CAP_TGSI_VS_LAYER_VIEWPORT);
      if (vs_layer) {
         st->clear.vs_layered = util_make_layered_clear_vertex_shader(pipe);
      } else {
         st->clear.vs_layered = util_make_layered_clear_helper_vertex_shader(pipe);
         st->clear.gs_layered = util_make_layered_clear_geometry_shader(pipe);
      }
   }

   cso_set_vertex_shader_handle(st->cso_context, st->clear.vs_layered);
   cso_set_geometry_shader_handle(st->cso_context, st->clear.gs_layered);
}


/**
 * Do glClear by drawing a quadrilateral.
 * The vertices of the quad will be computed from the
 * ctx->DrawBuffer->_X/Ymin/max fields.
 */
static void
clear_with_quad(struct gl_context *ctx, unsigned clear_buffers)
{
   struct st_context *st = st_context(ctx);
   struct cso_context *cso = st->cso_context;
   const struct gl_framebuffer *fb = ctx->DrawBuffer;
   const GLfloat fb_width = (GLfloat) fb->Width;
   const GLfloat fb_height = (GLfloat) fb->Height;
   const GLfloat x0 = (GLfloat) ctx->DrawBuffer->_Xmin / fb_width * 2.0f - 1.0f;
   const GLfloat x1 = (GLfloat) ctx->DrawBuffer->_Xmax / fb_width * 2.0f - 1.0f;
   const GLfloat y0 = (GLfloat) ctx->DrawBuffer->_Ymin / fb_height * 2.0f - 1.0f;
   const GLfloat y1 = (GLfloat) ctx->DrawBuffer->_Ymax / fb_height * 2.0f - 1.0f;
   unsigned num_layers =
      util_framebuffer_get_num_layers(&st->state.framebuffer);

   /*
   printf("%s %s%s%s %f,%f %f,%f\n", __func__,
	  color ? "color, " : "",
	  depth ? "depth, " : "",
	  stencil ? "stencil" : "",
	  x0, y0,
	  x1, y1);
   */

   cso_save_state(cso, (CSO_BIT_BLEND |
                        CSO_BIT_STENCIL_REF |
                        CSO_BIT_DEPTH_STENCIL_ALPHA |
                        CSO_BIT_RASTERIZER |
                        CSO_BIT_SAMPLE_MASK |
                        CSO_BIT_MIN_SAMPLES |
                        CSO_BIT_VIEWPORT |
                        CSO_BIT_STREAM_OUTPUTS |
                        CSO_BIT_VERTEX_ELEMENTS |
                        CSO_BIT_AUX_VERTEX_BUFFER_SLOT |
                        CSO_BIT_PAUSE_QUERIES |
                        CSO_BITS_ALL_SHADERS));

   /* blend state: RGBA masking */
   {
      struct pipe_blend_state blend;
      memset(&blend, 0, sizeof(blend));
      if (clear_buffers & PIPE_CLEAR_COLOR) {
         int num_buffers = ctx->Extensions.EXT_draw_buffers2 ?
                           ctx->DrawBuffer->_NumColorDrawBuffers : 1;
         int i;

         blend.independent_blend_enable = num_buffers > 1;

         for (i = 0; i < num_buffers; i++) {
            if (!(clear_buffers & (PIPE_CLEAR_COLOR0 << i)))
               continue;

            if (ctx->Color.ColorMask[i][0])
               blend.rt[i].colormask |= PIPE_MASK_R;
            if (ctx->Color.ColorMask[i][1])
               blend.rt[i].colormask |= PIPE_MASK_G;
            if (ctx->Color.ColorMask[i][2])
               blend.rt[i].colormask |= PIPE_MASK_B;
            if (ctx->Color.ColorMask[i][3])
               blend.rt[i].colormask |= PIPE_MASK_A;
         }

         if (ctx->Color.DitherFlag)
            blend.dither = 1;
      }
      cso_set_blend(cso, &blend);
   }

   /* depth_stencil state: always pass/set to ref value */
   {
      struct pipe_depth_stencil_alpha_state depth_stencil;
      memset(&depth_stencil, 0, sizeof(depth_stencil));
      if (clear_buffers & PIPE_CLEAR_DEPTH) {
         depth_stencil.depth.enabled = 1;
         depth_stencil.depth.writemask = 1;
         depth_stencil.depth.func = PIPE_FUNC_ALWAYS;
      }

      if (clear_buffers & PIPE_CLEAR_STENCIL) {
         struct pipe_stencil_ref stencil_ref;
         memset(&stencil_ref, 0, sizeof(stencil_ref));
         depth_stencil.stencil[0].enabled = 1;
         depth_stencil.stencil[0].func = PIPE_FUNC_ALWAYS;
         depth_stencil.stencil[0].fail_op = PIPE_STENCIL_OP_REPLACE;
         depth_stencil.stencil[0].zpass_op = PIPE_STENCIL_OP_REPLACE;
         depth_stencil.stencil[0].zfail_op = PIPE_STENCIL_OP_REPLACE;
         depth_stencil.stencil[0].valuemask = 0xff;
         depth_stencil.stencil[0].writemask = ctx->Stencil.WriteMask[0] & 0xff;
         stencil_ref.ref_value[0] = ctx->Stencil.Clear;
         cso_set_stencil_ref(cso, &stencil_ref);
      }

      cso_set_depth_stencil_alpha(cso, &depth_stencil);
   }

   cso_set_vertex_elements(cso, 2, st->util_velems);
   cso_set_stream_outputs(cso, 0, NULL, NULL);
   cso_set_sample_mask(cso, ~0);
   cso_set_min_samples(cso, 1);
   cso_set_rasterizer(cso, &st->clear.raster);

   /* viewport state: viewport matching window dims */
   cso_set_viewport_dims(st->cso_context, fb_width, fb_height,
                         st_fb_orientation(fb) == Y_0_TOP);

   set_fragment_shader(st);
   cso_set_tessctrl_shader_handle(cso, NULL);
   cso_set_tesseval_shader_handle(cso, NULL);

   if (num_layers > 1)
      set_vertex_shader_layered(st);
   else
      set_vertex_shader(st);

   /* draw quad matching scissor rect.
    *
    * Note: if we're only clearing depth/stencil we still setup vertices
    * with color, but they'll be ignored.
    *
    * We can't translate the clear color to the colorbuffer format,
    * because different colorbuffers may have different formats.
    */
   if (!st_draw_quad(st, x0, y0, x1, y1,
                     ctx->Depth.Clear * 2.0f - 1.0f,
                     0.0f, 0.0f, 0.0f, 0.0f,
                     (const float *) &ctx->Color.ClearColor.f,
                     num_layers)) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glClear");
   }

   /* Restore pipe state */
   cso_restore_state(cso);
}


/**
 * Return if the scissor must be enabled during the clear.
 */
static inline GLboolean
is_scissor_enabled(struct gl_context *ctx, struct gl_renderbuffer *rb)
{
   const struct gl_scissor_rect *scissor = &ctx->Scissor.ScissorArray[0];

   return (ctx->Scissor.EnableFlags & 1) &&
          (scissor->X > 0 ||
           scissor->Y > 0 ||
           scissor->X + scissor->Width < (int)rb->Width ||
           scissor->Y + scissor->Height < (int)rb->Height);
}

/**
 * Return if window rectangles must be enabled during the clear.
 */
static inline bool
is_window_rectangle_enabled(struct gl_context *ctx)
{
   if (ctx->DrawBuffer == ctx->WinSysDrawBuffer)
      return false;
   return ctx->Scissor.NumWindowRects > 0 ||
      ctx->Scissor.WindowRectMode == GL_INCLUSIVE_EXT;
}


/**
 * Return if all of the color channels are masked.
 */
static inline GLboolean
is_color_disabled(struct gl_context *ctx, int i)
{
   return !ctx->Color.ColorMask[i][0] &&
          !ctx->Color.ColorMask[i][1] &&
          !ctx->Color.ColorMask[i][2] &&
          !ctx->Color.ColorMask[i][3];
}


/**
 * Return if any of the color channels are masked.
 */
static inline GLboolean
is_color_masked(struct gl_context *ctx, int i)
{
   return !ctx->Color.ColorMask[i][0] ||
          !ctx->Color.ColorMask[i][1] ||
          !ctx->Color.ColorMask[i][2] ||
          !ctx->Color.ColorMask[i][3];
}


/**
 * Return if all of the stencil bits are masked.
 */
static inline GLboolean
is_stencil_disabled(struct gl_context *ctx, struct gl_renderbuffer *rb)
{
   const GLuint stencilMax = 0xff;

   assert(_mesa_get_format_bits(rb->Format, GL_STENCIL_BITS) > 0);
   return (ctx->Stencil.WriteMask[0] & stencilMax) == 0;
}


/**
 * Return if any of the stencil bits are masked.
 */
static inline GLboolean
is_stencil_masked(struct gl_context *ctx, struct gl_renderbuffer *rb)
{
   const GLuint stencilMax = 0xff;

   assert(_mesa_get_format_bits(rb->Format, GL_STENCIL_BITS) > 0);
   return (ctx->Stencil.WriteMask[0] & stencilMax) != stencilMax;
}


/**
 * Called via ctx->Driver.Clear()
 */
static void
st_Clear(struct gl_context *ctx, GLbitfield mask)
{
   struct st_context *st = st_context(ctx);
   struct gl_renderbuffer *depthRb
      = ctx->DrawBuffer->Attachment[BUFFER_DEPTH].Renderbuffer;
   struct gl_renderbuffer *stencilRb
      = ctx->DrawBuffer->Attachment[BUFFER_STENCIL].Renderbuffer;
   GLbitfield quad_buffers = 0x0;
   GLbitfield clear_buffers = 0x0;
   GLuint i;

   st_flush_bitmap_cache(st);
   st_invalidate_readpix_cache(st);

   /* This makes sure the pipe has the latest scissor, etc values */
   st_validate_state( st, ST_PIPELINE_RENDER );

   if (mask & BUFFER_BITS_COLOR) {
      for (i = 0; i < ctx->DrawBuffer->_NumColorDrawBuffers; i++) {
         GLint b = ctx->DrawBuffer->_ColorDrawBufferIndexes[i];

         if (b >= 0 && mask & (1 << b)) {
            struct gl_renderbuffer *rb
               = ctx->DrawBuffer->Attachment[b].Renderbuffer;
            struct st_renderbuffer *strb = st_renderbuffer(rb);
            int colormask_index = ctx->Extensions.EXT_draw_buffers2 ? i : 0;

            if (!strb || !strb->surface)
               continue;

            if (is_color_disabled(ctx, colormask_index))
               continue;

            if (is_scissor_enabled(ctx, rb) ||
                is_window_rectangle_enabled(ctx) ||
                is_color_masked(ctx, colormask_index))
               quad_buffers |= PIPE_CLEAR_COLOR0 << i;
            else
               clear_buffers |= PIPE_CLEAR_COLOR0 << i;
         }
      }
   }

   if (mask & BUFFER_BIT_DEPTH) {
      struct st_renderbuffer *strb = st_renderbuffer(depthRb);

      if (strb->surface && ctx->Depth.Mask) {
         if (is_scissor_enabled(ctx, depthRb) ||
             is_window_rectangle_enabled(ctx))
            quad_buffers |= PIPE_CLEAR_DEPTH;
         else
            clear_buffers |= PIPE_CLEAR_DEPTH;
      }
   }
   if (mask & BUFFER_BIT_STENCIL) {
      struct st_renderbuffer *strb = st_renderbuffer(stencilRb);

      if (strb->surface && !is_stencil_disabled(ctx, stencilRb)) {
         if (is_scissor_enabled(ctx, stencilRb) ||
             is_window_rectangle_enabled(ctx) ||
             is_stencil_masked(ctx, stencilRb))
            quad_buffers |= PIPE_CLEAR_STENCIL;
         else
            clear_buffers |= PIPE_CLEAR_STENCIL;
      }
   }

   /* Always clear depth and stencil together.
    * This can only happen when the stencil writemask is not a full mask.
    */
   if (quad_buffers & PIPE_CLEAR_DEPTHSTENCIL &&
       clear_buffers & PIPE_CLEAR_DEPTHSTENCIL) {
      quad_buffers |= clear_buffers & PIPE_CLEAR_DEPTHSTENCIL;
      clear_buffers &= ~PIPE_CLEAR_DEPTHSTENCIL;
   }

   /* Only use quad-based clearing for the renderbuffers which cannot
    * use pipe->clear. We want to always use pipe->clear for the other
    * renderbuffers, because it's likely to be faster.
    */
   if (quad_buffers) {
      clear_with_quad(ctx, quad_buffers);
   }
   if (clear_buffers) {
      /* We can't translate the clear color to the colorbuffer format,
       * because different colorbuffers may have different formats.
       */
      st->pipe->clear(st->pipe, clear_buffers,
                      (union pipe_color_union*)&ctx->Color.ClearColor,
                      ctx->Depth.Clear, ctx->Stencil.Clear);
   }
   if (mask & BUFFER_BIT_ACCUM)
      _mesa_clear_accum_buffer(ctx);
}


void
st_init_clear_functions(struct dd_function_table *functions)
{
   functions->Clear = st_Clear;
}
