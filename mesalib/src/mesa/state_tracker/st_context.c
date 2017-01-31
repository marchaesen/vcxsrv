/**************************************************************************
 * 
 * Copyright 2007 VMware, Inc.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 * 
 **************************************************************************/

#include "main/imports.h"
#include "main/accum.h"
#include "main/api_exec.h"
#include "main/context.h"
#include "main/samplerobj.h"
#include "main/shaderobj.h"
#include "main/version.h"
#include "main/vtxfmt.h"
#include "main/hash.h"
#include "program/prog_cache.h"
#include "vbo/vbo.h"
#include "glapi/glapi.h"
#include "st_context.h"
#include "st_debug.h"
#include "st_cb_bitmap.h"
#include "st_cb_blit.h"
#include "st_cb_bufferobjects.h"
#include "st_cb_clear.h"
#include "st_cb_compute.h"
#include "st_cb_condrender.h"
#include "st_cb_copyimage.h"
#include "st_cb_drawpixels.h"
#include "st_cb_rasterpos.h"
#include "st_cb_drawtex.h"
#include "st_cb_eglimage.h"
#include "st_cb_fbo.h"
#include "st_cb_feedback.h"
#include "st_cb_msaa.h"
#include "st_cb_perfmon.h"
#include "st_cb_program.h"
#include "st_cb_queryobj.h"
#include "st_cb_readpixels.h"
#include "st_cb_texture.h"
#include "st_cb_xformfb.h"
#include "st_cb_flush.h"
#include "st_cb_syncobj.h"
#include "st_cb_strings.h"
#include "st_cb_texturebarrier.h"
#include "st_cb_viewport.h"
#include "st_atom.h"
#include "st_draw.h"
#include "st_extensions.h"
#include "st_gen_mipmap.h"
#include "st_pbo.h"
#include "st_program.h"
#include "st_sampler_view.h"
#include "st_vdpau.h"
#include "st_texture.h"
#include "pipe/p_context.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include "cso_cache/cso_context.h"


DEBUG_GET_ONCE_BOOL_OPTION(mesa_mvp_dp4, "MESA_MVP_DP4", FALSE)


/**
 * Called via ctx->Driver.Enable()
 */
static void st_Enable(struct gl_context * ctx, GLenum cap, GLboolean state)
{
   struct st_context *st = st_context(ctx);

   switch (cap) {
   case GL_DEBUG_OUTPUT:
   case GL_DEBUG_OUTPUT_SYNCHRONOUS:
      st_update_debug_callback(st);
      break;
   default:
      break;
   }
}


/**
 * Called via ctx->Driver.QueryMemoryInfo()
 */
static void
st_query_memory_info(struct gl_context *ctx, struct gl_memory_info *out)
{
   struct pipe_screen *screen = st_context(ctx)->pipe->screen;
   struct pipe_memory_info info;

   assert(screen->query_memory_info);
   if (!screen->query_memory_info)
      return;

   screen->query_memory_info(screen, &info);

   out->total_device_memory = info.total_device_memory;
   out->avail_device_memory = info.avail_device_memory;
   out->total_staging_memory = info.total_staging_memory;
   out->avail_staging_memory = info.avail_staging_memory;
   out->device_memory_evicted = info.device_memory_evicted;
   out->nr_device_memory_evictions = info.nr_device_memory_evictions;
}


uint64_t
st_get_active_states(struct gl_context *ctx)
{
   struct st_vertex_program *vp =
      st_vertex_program(ctx->VertexProgram._Current);
   struct st_tessctrl_program *tcp =
      st_tessctrl_program(ctx->TessCtrlProgram._Current);
   struct st_tesseval_program *tep =
      st_tesseval_program(ctx->TessEvalProgram._Current);
   struct st_geometry_program *gp =
      st_geometry_program(ctx->GeometryProgram._Current);
   struct st_fragment_program *fp =
      st_fragment_program(ctx->FragmentProgram._Current);
   struct st_compute_program *cp =
      st_compute_program(ctx->ComputeProgram._Current);
   uint64_t active_shader_states = 0;

   if (vp)
      active_shader_states |= vp->affected_states;
   if (tcp)
      active_shader_states |= tcp->affected_states;
   if (tep)
      active_shader_states |= tep->affected_states;
   if (gp)
      active_shader_states |= gp->affected_states;
   if (fp)
      active_shader_states |= fp->affected_states;
   if (cp)
      active_shader_states |= cp->affected_states;

   /* Mark non-shader-resource shader states as "always active". */
   return active_shader_states | ~ST_ALL_SHADER_RESOURCES;
}


/**
 * Called via ctx->Driver.UpdateState()
 */
void st_invalidate_state(struct gl_context * ctx, GLbitfield new_state)
{
   struct st_context *st = st_context(ctx);

   if (new_state & _NEW_BUFFERS) {
      st->dirty |= ST_NEW_BLEND |
                   ST_NEW_DSA |
                   ST_NEW_FB_STATE |
                   ST_NEW_SAMPLE_MASK |
                   ST_NEW_SAMPLE_SHADING |
                   ST_NEW_FS_STATE |
                   ST_NEW_POLY_STIPPLE |
                   ST_NEW_VIEWPORT |
                   ST_NEW_RASTERIZER |
                   ST_NEW_SCISSOR |
                   ST_NEW_WINDOW_RECTANGLES;
   } else {
      /* These set a subset of flags set by _NEW_BUFFERS, so we only have to
       * check them when _NEW_BUFFERS isn't set.
       */
      if (new_state & (_NEW_DEPTH |
                       _NEW_STENCIL))
         st->dirty |= ST_NEW_DSA;

      if (new_state & _NEW_PROGRAM)
         st->dirty |= ST_NEW_RASTERIZER;

      if (new_state & _NEW_SCISSOR)
         st->dirty |= ST_NEW_RASTERIZER |
                      ST_NEW_SCISSOR |
                      ST_NEW_WINDOW_RECTANGLES;

      if (new_state & _NEW_FOG)
         st->dirty |= ST_NEW_FS_STATE;

      if (new_state & _NEW_POLYGONSTIPPLE)
         st->dirty |= ST_NEW_POLY_STIPPLE;

      if (new_state & _NEW_VIEWPORT)
         st->dirty |= ST_NEW_VIEWPORT;

      if (new_state & _NEW_FRAG_CLAMP) {
         if (st->clamp_frag_color_in_shader)
            st->dirty |= ST_NEW_FS_STATE;
         else
            st->dirty |= ST_NEW_RASTERIZER;
      }
   }

   if (new_state & _NEW_MULTISAMPLE) {
      st->dirty |= ST_NEW_BLEND |
                   ST_NEW_SAMPLE_MASK |
                   ST_NEW_SAMPLE_SHADING |
                   ST_NEW_RASTERIZER |
                   ST_NEW_FS_STATE;
   } else {
      /* These set a subset of flags set by _NEW_MULTISAMPLE, so we only
       * have to check them when _NEW_MULTISAMPLE isn't set.
       */
      if (new_state & (_NEW_LIGHT |
                       _NEW_LINE |
                       _NEW_POINT |
                       _NEW_POLYGON |
                       _NEW_TRANSFORM))
         st->dirty |= ST_NEW_RASTERIZER;
   }

   if (new_state & (_NEW_PROJECTION |
                    _NEW_TRANSFORM) &&
       st_user_clip_planes_enabled(ctx))
      st->dirty |= ST_NEW_CLIP_STATE;

   if (new_state & _NEW_COLOR)
      st->dirty |= ST_NEW_BLEND |
                   ST_NEW_DSA;

   if (new_state & _NEW_PIXEL)
      st->dirty |= ST_NEW_PIXEL_TRANSFER;

   if (new_state & _NEW_CURRENT_ATTRIB)
      st->dirty |= ST_NEW_VERTEX_ARRAYS;

   /* Update the vertex shader if ctx->Light._ClampVertexColor was changed. */
   if (st->clamp_vert_color_in_shader && (new_state & _NEW_LIGHT))
      st->dirty |= ST_NEW_VS_STATE;

   /* Which shaders are dirty will be determined manually. */
   if (new_state & _NEW_PROGRAM) {
      st->gfx_shaders_may_be_dirty = true;
      st->compute_shader_may_be_dirty = true;
      /* This will mask out unused shader resources. */
      st->active_states = st_get_active_states(ctx);
   }

   if (new_state & _NEW_TEXTURE) {
      st->dirty |= st->active_states &
                   (ST_NEW_SAMPLER_VIEWS |
                    ST_NEW_SAMPLERS |
                    ST_NEW_IMAGE_UNITS);
      if (ctx->FragmentProgram._Current &&
          ctx->FragmentProgram._Current->ExternalSamplersUsed) {
         st->dirty |= ST_NEW_FS_STATE;
      }
   }

   if (new_state & _NEW_PROGRAM_CONSTANTS)
      st->dirty |= st->active_states & ST_NEW_CONSTANTS;

   /* This is the only core Mesa module we depend upon.
    * No longer use swrast, swsetup, tnl.
    */
   _vbo_InvalidateState(ctx, new_state);
}


static void
st_destroy_context_priv(struct st_context *st, bool destroy_pipe)
{
   uint shader, i;

   st_destroy_atoms( st );
   st_destroy_draw( st );
   st_destroy_clear(st);
   st_destroy_bitmap(st);
   st_destroy_drawpix(st);
   st_destroy_drawtex(st);
   st_destroy_perfmon(st);
   st_destroy_pbo_helpers(st);

   for (shader = 0; shader < ARRAY_SIZE(st->state.sampler_views); shader++) {
      for (i = 0; i < ARRAY_SIZE(st->state.sampler_views[0]); i++) {
         pipe_sampler_view_release(st->pipe,
                                   &st->state.sampler_views[shader][i]);
      }
   }

   u_upload_destroy(st->uploader);
   if (st->indexbuf_uploader) {
      u_upload_destroy(st->indexbuf_uploader);
   }
   if (st->constbuf_uploader) {
      u_upload_destroy(st->constbuf_uploader);
   }

   /* free glDrawPixels cache data */
   free(st->drawpix_cache.image);
   pipe_resource_reference(&st->drawpix_cache.texture, NULL);

   /* free glReadPixels cache data */
   st_invalidate_readpix_cache(st);

   cso_destroy_context(st->cso_context);

   if (st->pipe && destroy_pipe)
      st->pipe->destroy(st->pipe);

   free( st );
}


static struct st_context *
st_create_context_priv( struct gl_context *ctx, struct pipe_context *pipe,
		const struct st_config_options *options)
{
   struct pipe_screen *screen = pipe->screen;
   uint i;
   struct st_context *st = ST_CALLOC_STRUCT( st_context );
   
   st->options = *options;

   ctx->st = st;

   st->ctx = ctx;
   st->pipe = pipe;

   /* XXX: this is one-off, per-screen init: */
   st_debug_init();
   
   /* state tracker needs the VBO module */
   _vbo_CreateContext(ctx);

   st->dirty = ST_ALL_STATES_MASK;

   /* Create upload manager for vertex data for glBitmap, glDrawPixels,
    * glClear, etc.
    */
   st->uploader = u_upload_create(pipe, 65536, PIPE_BIND_VERTEX_BUFFER,
                                  PIPE_USAGE_STREAM);

   if (!screen->get_param(screen, PIPE_CAP_USER_INDEX_BUFFERS)) {
      st->indexbuf_uploader = u_upload_create(pipe, 128 * 1024,
                                              PIPE_BIND_INDEX_BUFFER,
                                              PIPE_USAGE_STREAM);
   }

   if (!screen->get_param(screen, PIPE_CAP_USER_CONSTANT_BUFFERS))
      st->constbuf_uploader = u_upload_create(pipe, 128 * 1024,
                                              PIPE_BIND_CONSTANT_BUFFER,
                                              PIPE_USAGE_STREAM);

   st->cso_context = cso_create_context(pipe);

   st_init_atoms( st );
   st_init_clear(st);
   st_init_draw( st );
   st_init_pbo_helpers(st);

   /* Choose texture target for glDrawPixels, glBitmap, renderbuffers */
   if (pipe->screen->get_param(pipe->screen, PIPE_CAP_NPOT_TEXTURES))
      st->internal_target = PIPE_TEXTURE_2D;
   else
      st->internal_target = PIPE_TEXTURE_RECT;

   /* Setup vertex element info for 'struct st_util_vertex'.
    */
   {
      const unsigned slot = cso_get_aux_vertex_buffer_slot(st->cso_context);

      /* If this assertion ever fails all state tracker calls to
       * cso_get_aux_vertex_buffer_slot() should be audited.  This
       * particular call would have to be moved to just before each
       * drawing call.
       */
      assert(slot == 0);

      STATIC_ASSERT(sizeof(struct st_util_vertex) == 9 * sizeof(float));

      memset(&st->util_velems, 0, sizeof(st->util_velems));
      st->util_velems[0].src_offset = 0;
      st->util_velems[0].vertex_buffer_index = slot;
      st->util_velems[0].src_format = PIPE_FORMAT_R32G32B32_FLOAT;
      st->util_velems[1].src_offset = 3 * sizeof(float);
      st->util_velems[1].vertex_buffer_index = slot;
      st->util_velems[1].src_format = PIPE_FORMAT_R32G32B32A32_FLOAT;
      st->util_velems[2].src_offset = 7 * sizeof(float);
      st->util_velems[2].vertex_buffer_index = slot;
      st->util_velems[2].src_format = PIPE_FORMAT_R32G32_FLOAT;
   }

   /* we want all vertex data to be placed in buffer objects */
   vbo_use_buffer_objects(ctx);


   /* make sure that no VBOs are left mapped when we're drawing. */
   vbo_always_unmap_buffers(ctx);

   /* Need these flags:
    */
   ctx->FragmentProgram._MaintainTexEnvProgram = GL_TRUE;

   ctx->VertexProgram._MaintainTnlProgram = GL_TRUE;

   st->has_stencil_export =
      screen->get_param(screen, PIPE_CAP_SHADER_STENCIL_EXPORT);
   st->has_shader_model3 = screen->get_param(screen, PIPE_CAP_SM3);
   st->has_etc1 = screen->is_format_supported(screen, PIPE_FORMAT_ETC1_RGB8,
                                              PIPE_TEXTURE_2D, 0,
                                              PIPE_BIND_SAMPLER_VIEW);
   st->has_etc2 = screen->is_format_supported(screen, PIPE_FORMAT_ETC2_RGB8,
                                              PIPE_TEXTURE_2D, 0,
                                              PIPE_BIND_SAMPLER_VIEW);
   st->prefer_blit_based_texture_transfer = screen->get_param(screen,
                              PIPE_CAP_PREFER_BLIT_BASED_TEXTURE_TRANSFER);
   st->force_persample_in_shader =
      screen->get_param(screen, PIPE_CAP_SAMPLE_SHADING) &&
      !screen->get_param(screen, PIPE_CAP_FORCE_PERSAMPLE_INTERP);
   st->has_shareable_shaders = screen->get_param(screen,
                                                 PIPE_CAP_SHAREABLE_SHADERS);
   st->needs_texcoord_semantic =
      screen->get_param(screen, PIPE_CAP_TGSI_TEXCOORD);
   st->apply_texture_swizzle_to_border_color =
      !!(screen->get_param(screen, PIPE_CAP_TEXTURE_BORDER_COLOR_QUIRK) &
         (PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_NV50 |
          PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600));
   st->has_time_elapsed =
      screen->get_param(screen, PIPE_CAP_QUERY_TIME_ELAPSED);
   st->has_half_float_packing =
      screen->get_param(screen, PIPE_CAP_TGSI_PACK_HALF_FLOAT);
   st->has_multi_draw_indirect =
      screen->get_param(screen, PIPE_CAP_MULTI_DRAW_INDIRECT);

   /* GL limits and extensions */
   st_init_limits(pipe->screen, &ctx->Const, &ctx->Extensions);
   st_init_extensions(pipe->screen, &ctx->Const,
                      &ctx->Extensions, &st->options, ctx->Mesa_DXTn);

   if (st_have_perfmon(st)) {
      ctx->Extensions.AMD_performance_monitor = GL_TRUE;
   }

   /* Enable shader-based fallbacks for ARB_color_buffer_float if needed. */
   if (screen->get_param(screen, PIPE_CAP_VERTEX_COLOR_UNCLAMPED)) {
      if (!screen->get_param(screen, PIPE_CAP_VERTEX_COLOR_CLAMPED)) {
         st->clamp_vert_color_in_shader = GL_TRUE;
      }

      if (!screen->get_param(screen, PIPE_CAP_FRAGMENT_COLOR_CLAMPED)) {
         st->clamp_frag_color_in_shader = GL_TRUE;
      }

      /* For drivers which cannot do color clamping, it's better to just
       * disable ARB_color_buffer_float in the core profile, because
       * the clamping is deprecated there anyway. */
      if (ctx->API == API_OPENGL_CORE &&
          (st->clamp_frag_color_in_shader || st->clamp_vert_color_in_shader)) {
         st->clamp_vert_color_in_shader = GL_FALSE;
         st->clamp_frag_color_in_shader = GL_FALSE;
         ctx->Extensions.ARB_color_buffer_float = GL_FALSE;
      }
   }

   /* called after _mesa_create_context/_mesa_init_point, fix default user
    * settable max point size up
    */
   ctx->Point.MaxSize = MAX2(ctx->Const.MaxPointSize,
                             ctx->Const.MaxPointSizeAA);
   /* For vertex shaders, make sure not to emit saturate when SM 3.0 is not supported */
   ctx->Const.ShaderCompilerOptions[MESA_SHADER_VERTEX].EmitNoSat = !st->has_shader_model3;

   if (!ctx->Extensions.ARB_gpu_shader5) {
      for (i = 0; i < MESA_SHADER_STAGES; i++)
         ctx->Const.ShaderCompilerOptions[i].EmitNoIndirectSampler = true;
   }

   /* Set which shader types can be compiled at link time. */
   st->shader_has_one_variant[MESA_SHADER_VERTEX] =
         st->has_shareable_shaders &&
         !st->clamp_vert_color_in_shader;

   st->shader_has_one_variant[MESA_SHADER_FRAGMENT] =
         st->has_shareable_shaders &&
         !st->clamp_frag_color_in_shader &&
         !st->force_persample_in_shader;

   st->shader_has_one_variant[MESA_SHADER_TESS_CTRL] = st->has_shareable_shaders;
   st->shader_has_one_variant[MESA_SHADER_TESS_EVAL] = st->has_shareable_shaders;
   st->shader_has_one_variant[MESA_SHADER_GEOMETRY] = st->has_shareable_shaders;
   st->shader_has_one_variant[MESA_SHADER_COMPUTE] = st->has_shareable_shaders;

   _mesa_compute_version(ctx);

   if (ctx->Version == 0) {
      /* This can happen when a core profile was requested, but the driver
       * does not support some features of GL 3.1 or later.
       */
      st_destroy_context_priv(st, false);
      return NULL;
   }

   _mesa_initialize_dispatch_tables(ctx);
   _mesa_initialize_vbo_vtxfmt(ctx);

   return st;
}

static void st_init_driver_flags(struct gl_driver_flags *f)
{
   f->NewArray = ST_NEW_VERTEX_ARRAYS;
   f->NewRasterizerDiscard = ST_NEW_RASTERIZER;
   f->NewUniformBuffer = ST_NEW_UNIFORM_BUFFER;
   f->NewDefaultTessLevels = ST_NEW_TESS_STATE;
   f->NewTextureBuffer = ST_NEW_SAMPLER_VIEWS;
   f->NewAtomicBuffer = ST_NEW_ATOMIC_BUFFER;
   f->NewShaderStorageBuffer = ST_NEW_STORAGE_BUFFER;
   f->NewImageUnits = ST_NEW_IMAGE_UNITS;
}

struct st_context *st_create_context(gl_api api, struct pipe_context *pipe,
                                     const struct gl_config *visual,
                                     struct st_context *share,
                                     const struct st_config_options *options)
{
   struct gl_context *ctx;
   struct gl_context *shareCtx = share ? share->ctx : NULL;
   struct dd_function_table funcs;
   struct st_context *st;

   memset(&funcs, 0, sizeof(funcs));
   st_init_driver_functions(pipe->screen, &funcs);

   ctx = calloc(1, sizeof(struct gl_context));
   if (!ctx)
      return NULL;

   if (!_mesa_initialize_context(ctx, api, visual, shareCtx, &funcs)) {
      free(ctx);
      return NULL;
   }

   st_init_driver_flags(&ctx->DriverFlags);

   /* XXX: need a capability bit in gallium to query if the pipe
    * driver prefers DP4 or MUL/MAD for vertex transformation.
    */
   if (debug_get_option_mesa_mvp_dp4())
      ctx->Const.ShaderCompilerOptions[MESA_SHADER_VERTEX].OptimizeForAOS = GL_TRUE;

   st = st_create_context_priv(ctx, pipe, options);
   if (!st) {
      _mesa_destroy_context(ctx);
   }

   return st;
}


/**
 * Callback to release the sampler view attached to a texture object.
 * Called by _mesa_HashWalk().
 */
static void
destroy_tex_sampler_cb(GLuint id, void *data, void *userData)
{
   struct gl_texture_object *texObj = (struct gl_texture_object *) data;
   struct st_context *st = (struct st_context *) userData;

   st_texture_release_sampler_view(st, st_texture_object(texObj));
}
 
void st_destroy_context( struct st_context *st )
{
   struct gl_context *ctx = st->ctx;
   GLuint i;

   _mesa_HashWalk(ctx->Shared->TexObjects, destroy_tex_sampler_cb, st);

   st_reference_fragprog(st, &st->fp, NULL);
   st_reference_geomprog(st, &st->gp, NULL);
   st_reference_vertprog(st, &st->vp, NULL);
   st_reference_tesscprog(st, &st->tcp, NULL);
   st_reference_tesseprog(st, &st->tep, NULL);
   st_reference_compprog(st, &st->cp, NULL);

   /* release framebuffer surfaces */
   for (i = 0; i < PIPE_MAX_COLOR_BUFS; i++) {
      pipe_surface_reference(&st->state.framebuffer.cbufs[i], NULL);
   }
   pipe_surface_reference(&st->state.framebuffer.zsbuf, NULL);
   pipe_sampler_view_reference(&st->pixel_xfer.pixelmap_sampler_view, NULL);
   pipe_resource_reference(&st->pixel_xfer.pixelmap_texture, NULL);

   _vbo_DestroyContext(ctx);

   st_destroy_program_variants(st);

   _mesa_free_context_data(ctx);

   /* This will free the st_context too, so 'st' must not be accessed
    * afterwards. */
   st_destroy_context_priv(st, true);
   st = NULL;

   free(ctx);
}

static void
st_emit_string_marker(struct gl_context *ctx, const GLchar *string, GLsizei len)
{
   struct st_context *st = ctx->st;
   st->pipe->emit_string_marker(st->pipe, string, len);
}

void st_init_driver_functions(struct pipe_screen *screen,
                              struct dd_function_table *functions)
{
   _mesa_init_shader_object_functions(functions);
   _mesa_init_sampler_object_functions(functions);

   st_init_blit_functions(functions);
   st_init_bufferobject_functions(screen, functions);
   st_init_clear_functions(functions);
   st_init_bitmap_functions(functions);
   st_init_copy_image_functions(functions);
   st_init_drawpixels_functions(functions);
   st_init_rasterpos_functions(functions);

   st_init_drawtex_functions(functions);

   st_init_eglimage_functions(functions);

   st_init_fbo_functions(functions);
   st_init_feedback_functions(functions);
   st_init_msaa_functions(functions);
   st_init_perfmon_functions(functions);
   st_init_program_functions(functions);
   st_init_query_functions(functions);
   st_init_cond_render_functions(functions);
   st_init_readpixels_functions(functions);
   st_init_texture_functions(functions);
   st_init_texture_barrier_functions(functions);
   st_init_flush_functions(screen, functions);
   st_init_string_functions(functions);
   st_init_viewport_functions(functions);
   st_init_compute_functions(functions);

   st_init_xformfb_functions(functions);
   st_init_syncobj_functions(functions);

   st_init_vdpau_functions(functions);

   if (screen->get_param(screen, PIPE_CAP_STRING_MARKER))
      functions->EmitStringMarker = st_emit_string_marker;

   functions->Enable = st_Enable;
   functions->UpdateState = st_invalidate_state;
   functions->QueryMemoryInfo = st_query_memory_info;
}
