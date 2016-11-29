/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

/**
 * Meta operations.  Some GL operations can be expressed in terms of
 * other GL operations.  For example, glBlitFramebuffer() can be done
 * with texture mapping and glClear() can be done with polygon rendering.
 *
 * \author Brian Paul
 */


#include "main/glheader.h"
#include "main/mtypes.h"
#include "main/imports.h"
#include "main/arbprogram.h"
#include "main/arrayobj.h"
#include "main/blend.h"
#include "main/blit.h"
#include "main/bufferobj.h"
#include "main/buffers.h"
#include "main/clear.h"
#include "main/condrender.h"
#include "main/depth.h"
#include "main/enable.h"
#include "main/fbobject.h"
#include "main/feedback.h"
#include "main/formats.h"
#include "main/format_unpack.h"
#include "main/framebuffer.h"
#include "main/glformats.h"
#include "main/image.h"
#include "main/macros.h"
#include "main/matrix.h"
#include "main/mipmap.h"
#include "main/multisample.h"
#include "main/objectlabel.h"
#include "main/pipelineobj.h"
#include "main/pixel.h"
#include "main/pbo.h"
#include "main/polygon.h"
#include "main/queryobj.h"
#include "main/readpix.h"
#include "main/renderbuffer.h"
#include "main/scissor.h"
#include "main/shaderapi.h"
#include "main/shaderobj.h"
#include "main/state.h"
#include "main/stencil.h"
#include "main/texobj.h"
#include "main/texenv.h"
#include "main/texgetimage.h"
#include "main/teximage.h"
#include "main/texparam.h"
#include "main/texstate.h"
#include "main/texstore.h"
#include "main/transformfeedback.h"
#include "main/uniforms.h"
#include "main/varray.h"
#include "main/viewport.h"
#include "main/samplerobj.h"
#include "program/program.h"
#include "swrast/swrast.h"
#include "drivers/common/meta.h"
#include "main/enums.h"
#include "main/glformats.h"
#include "util/bitscan.h"
#include "util/ralloc.h"

/** Return offset in bytes of the field within a vertex struct */
#define OFFSET(FIELD) ((void *) offsetof(struct vertex, FIELD))

static void
meta_clear(struct gl_context *ctx, GLbitfield buffers, bool glsl);

static struct blit_shader *
choose_blit_shader(GLenum target, struct blit_shader_table *table);

static void cleanup_temp_texture(struct temp_texture *tex);
static void meta_glsl_clear_cleanup(struct gl_context *ctx,
                                    struct clear_state *clear);
static void meta_decompress_cleanup(struct gl_context *ctx,
                                    struct decompress_state *decompress);
static void meta_drawpix_cleanup(struct gl_context *ctx,
                                 struct drawpix_state *drawpix);

void
_mesa_meta_framebuffer_texture_image(struct gl_context *ctx,
                                     struct gl_framebuffer *fb,
                                     GLenum attachment,
                                     struct gl_texture_image *texImage,
                                     GLuint layer)
{
   struct gl_texture_object *texObj = texImage->TexObject;
   int level = texImage->Level;
   const GLenum texTarget = texObj->Target == GL_TEXTURE_CUBE_MAP
      ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + texImage->Face
      : texObj->Target;

   _mesa_framebuffer_texture(ctx, fb, attachment, texObj, texTarget,
                             level, layer, false, __func__);
}

static struct gl_shader *
meta_compile_shader_with_debug(struct gl_context *ctx, gl_shader_stage stage,
                               const GLcharARB *source)
{
   const GLuint name = ~0;
   struct gl_shader *sh;

   sh = _mesa_new_shader(name, stage);
   sh->Source = strdup(source);
   sh->CompileStatus = false;
   _mesa_compile_shader(ctx, sh);

   if (!sh->CompileStatus) {
      if (sh->InfoLog) {
         _mesa_problem(ctx,
                       "meta program compile failed:\n%s\nsource:\n%s\n",
                       sh->InfoLog, source);
      }

      _mesa_reference_shader(ctx, &sh, NULL);
   }

   return sh;
}

void
_mesa_meta_link_program_with_debug(struct gl_context *ctx,
                                   struct gl_shader_program *sh_prog)
{
   _mesa_link_program(ctx, sh_prog);

   if (!sh_prog->data->LinkStatus) {
      _mesa_problem(ctx, "meta program link failed:\n%s",
                    sh_prog->data->InfoLog);
   }
}

void
_mesa_meta_use_program(struct gl_context *ctx,
                       struct gl_shader_program *sh_prog)
{
   /* Attach shader state to the binding point */
   _mesa_reference_pipeline_object(ctx, &ctx->_Shader, &ctx->Shader);

   /* Update the program */
   _mesa_use_program(ctx, sh_prog);
}

void
_mesa_meta_compile_and_link_program(struct gl_context *ctx,
                                    const char *vs_source,
                                    const char *fs_source,
                                    const char *name,
                                    struct gl_shader_program **out_sh_prog)
{
   struct gl_shader_program *sh_prog;
   const GLuint id = ~0;

   sh_prog = _mesa_new_shader_program(id);
   sh_prog->Label = strdup(name);
   sh_prog->NumShaders = 2;
   sh_prog->Shaders = malloc(2 * sizeof(struct gl_shader *));
   sh_prog->Shaders[0] =
      meta_compile_shader_with_debug(ctx, MESA_SHADER_VERTEX, vs_source);
   sh_prog->Shaders[1] =
      meta_compile_shader_with_debug(ctx, MESA_SHADER_FRAGMENT, fs_source);

   _mesa_meta_link_program_with_debug(ctx, sh_prog);

   _mesa_meta_use_program(ctx, sh_prog);

   *out_sh_prog = sh_prog;
}

/**
 * Generate a generic shader to blit from a texture to a framebuffer
 *
 * \param ctx       Current GL context
 * \param texTarget Texture target that will be the source of the blit
 *
 * \returns a handle to a shader program on success or zero on failure.
 */
void
_mesa_meta_setup_blit_shader(struct gl_context *ctx,
                             GLenum target,
                             bool do_depth,
                             struct blit_shader_table *table)
{
   char *vs_source, *fs_source;
   struct blit_shader *shader = choose_blit_shader(target, table);
   const char *fs_input, *vs_preprocess, *fs_preprocess;
   void *mem_ctx;

   if (ctx->Const.GLSLVersion < 130) {
      vs_preprocess = "";
      fs_preprocess = "#extension GL_EXT_texture_array : enable";
      fs_input = "varying";
   } else {
      vs_preprocess = "#version 130";
      fs_preprocess = "#version 130";
      fs_input = "in";
      shader->func = "texture";
   }

   assert(shader != NULL);

   if (shader->shader_prog != NULL) {
      _mesa_meta_use_program(ctx, shader->shader_prog);
      return;
   }

   mem_ctx = ralloc_context(NULL);

   vs_source = ralloc_asprintf(mem_ctx,
                "%s\n"
                "#extension GL_ARB_explicit_attrib_location: enable\n"
                "layout(location = 0) in vec2 position;\n"
                "layout(location = 1) in vec4 textureCoords;\n"
                "out vec4 texCoords;\n"
                "void main()\n"
                "{\n"
                "   texCoords = textureCoords;\n"
                "   gl_Position = vec4(position, 0.0, 1.0);\n"
                "}\n",
                vs_preprocess);

   fs_source = ralloc_asprintf(mem_ctx,
                "%s\n"
                "#extension GL_ARB_texture_cube_map_array: enable\n"
                "uniform %s texSampler;\n"
                "%s vec4 texCoords;\n"
                "void main()\n"
                "{\n"
                "   gl_FragColor = %s(texSampler, %s);\n"
                "%s"
                "}\n",
                fs_preprocess, shader->type, fs_input,
                shader->func, shader->texcoords,
                do_depth ?  "   gl_FragDepth = gl_FragColor.x;\n" : "");

   _mesa_meta_compile_and_link_program(ctx, vs_source, fs_source,
                                       ralloc_asprintf(mem_ctx, "%s blit",
                                                       shader->type),
                                       &shader->shader_prog);
   ralloc_free(mem_ctx);
}

/**
 * Configure vertex buffer and vertex array objects for tests
 *
 * Regardless of whether a new VAO is created, the object referenced by \c VAO
 * will be bound into the GL state vector when this function terminates.  The
 * object referenced by \c VBO will \b not be bound.
 *
 * \param VAO       Storage for vertex array object handle.  If 0, a new VAO
 *                  will be created.
 * \param buf_obj   Storage for vertex buffer object pointer.  If \c NULL, a new VBO
 *                  will be created.  The new VBO will have storage for 4
 *                  \c vertex structures.
 * \param use_generic_attributes  Should generic attributes 0 and 1 be used,
 *                  or should traditional, fixed-function color and texture
 *                  coordinate be used?
 * \param vertex_size  Number of components for attribute 0 / vertex.
 * \param texcoord_size  Number of components for attribute 1 / texture
 *                  coordinate.  If this is 0, attribute 1 will not be set or
 *                  enabled.
 * \param color_size  Number of components for attribute 1 / primary color.
 *                  If this is 0, attribute 1 will not be set or enabled.
 *
 * \note If \c use_generic_attributes is \c true, \c color_size must be zero.
 * Use \c texcoord_size instead.
 */
void
_mesa_meta_setup_vertex_objects(struct gl_context *ctx,
                                GLuint *VAO, struct gl_buffer_object **buf_obj,
                                bool use_generic_attributes,
                                unsigned vertex_size, unsigned texcoord_size,
                                unsigned color_size)
{
   if (*VAO == 0) {
      struct gl_vertex_array_object *array_obj;
      assert(*buf_obj == NULL);

      /* create vertex array object */
      _mesa_GenVertexArrays(1, VAO);
      _mesa_BindVertexArray(*VAO);

      array_obj = _mesa_lookup_vao(ctx, *VAO);
      assert(array_obj != NULL);

      /* create vertex array buffer */
      *buf_obj = ctx->Driver.NewBufferObject(ctx, 0xDEADBEEF);
      if (*buf_obj == NULL)
         return;

      _mesa_buffer_data(ctx, *buf_obj, GL_NONE, 4 * sizeof(struct vertex), NULL,
                        GL_DYNAMIC_DRAW, __func__);

      /* setup vertex arrays */
      if (use_generic_attributes) {
         assert(color_size == 0);

         _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_GENERIC(0),
                                   vertex_size, GL_FLOAT, GL_RGBA, GL_FALSE,
                                   GL_FALSE, GL_FALSE,
                                   offsetof(struct vertex, x), true);
         _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_GENERIC(0),
                                  *buf_obj, 0, sizeof(struct vertex));
         _mesa_enable_vertex_array_attrib(ctx, array_obj,
                                          VERT_ATTRIB_GENERIC(0));
         if (texcoord_size > 0) {
            _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_GENERIC(1),
                                      texcoord_size, GL_FLOAT, GL_RGBA,
                                      GL_FALSE, GL_FALSE, GL_FALSE,
                                      offsetof(struct vertex, tex), false);
            _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_GENERIC(1),
                                     *buf_obj, 0, sizeof(struct vertex));
            _mesa_enable_vertex_array_attrib(ctx, array_obj,
                                             VERT_ATTRIB_GENERIC(1));
         }
      } else {
         _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_POS,
                                   vertex_size, GL_FLOAT, GL_RGBA, GL_FALSE,
                                   GL_FALSE, GL_FALSE,
                                   offsetof(struct vertex, x), true);
         _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_POS,
                                  *buf_obj, 0, sizeof(struct vertex));
         _mesa_enable_vertex_array_attrib(ctx, array_obj, VERT_ATTRIB_POS);

         if (texcoord_size > 0) {
            _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_TEX(0),
                                      vertex_size, GL_FLOAT, GL_RGBA, GL_FALSE,
                                      GL_FALSE, GL_FALSE,
                                      offsetof(struct vertex, tex), false);
            _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_TEX(0),
                                     *buf_obj, 0, sizeof(struct vertex));
            _mesa_enable_vertex_array_attrib(ctx, array_obj, VERT_ATTRIB_TEX(0));
         }

         if (color_size > 0) {
            _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_COLOR0,
                                      vertex_size, GL_FLOAT, GL_RGBA, GL_FALSE,
                                      GL_FALSE, GL_FALSE,
                                      offsetof(struct vertex, r), false);
            _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_COLOR0,
                                     *buf_obj, 0, sizeof(struct vertex));
            _mesa_enable_vertex_array_attrib(ctx, array_obj, VERT_ATTRIB_COLOR0);
         }
      }
   } else {
      _mesa_BindVertexArray(*VAO);
   }
}

/**
 * Initialize meta-ops for a context.
 * To be called once during context creation.
 */
void
_mesa_meta_init(struct gl_context *ctx)
{
   assert(!ctx->Meta);

   ctx->Meta = CALLOC_STRUCT(gl_meta_state);
}

/**
 * Free context meta-op state.
 * To be called once during context destruction.
 */
void
_mesa_meta_free(struct gl_context *ctx)
{
   GET_CURRENT_CONTEXT(old_context);
   _mesa_make_current(ctx, NULL, NULL);
   _mesa_meta_glsl_blit_cleanup(ctx, &ctx->Meta->Blit);
   meta_glsl_clear_cleanup(ctx, &ctx->Meta->Clear);
   _mesa_meta_glsl_generate_mipmap_cleanup(ctx, &ctx->Meta->Mipmap);
   cleanup_temp_texture(&ctx->Meta->TempTex);
   meta_decompress_cleanup(ctx, &ctx->Meta->Decompress);
   meta_drawpix_cleanup(ctx, &ctx->Meta->DrawPix);
   if (old_context)
      _mesa_make_current(old_context, old_context->WinSysDrawBuffer, old_context->WinSysReadBuffer);
   else
      _mesa_make_current(NULL, NULL, NULL);
   free(ctx->Meta);
   ctx->Meta = NULL;
}


/**
 * Enter meta state.  This is like a light-weight version of glPushAttrib
 * but it also resets most GL state back to default values.
 *
 * \param state  bitmask of MESA_META_* flags indicating which attribute groups
 *               to save and reset to their defaults
 */
void
_mesa_meta_begin(struct gl_context *ctx, GLbitfield state)
{
   struct save_state *save;

   /* hope MAX_META_OPS_DEPTH is large enough */
   assert(ctx->Meta->SaveStackDepth < MAX_META_OPS_DEPTH);

   save = &ctx->Meta->Save[ctx->Meta->SaveStackDepth++];
   memset(save, 0, sizeof(*save));
   save->SavedState = state;

   /* We always push into desktop GL mode and pop out at the end.  No sense in
    * writing our shaders varying based on the user's context choice, when
    * Mesa can handle either.
    */
   save->API = ctx->API;
   ctx->API = API_OPENGL_COMPAT;

   /* Mesa's extension helper functions use the current context's API to look up
    * the version required by an extension as a step in determining whether or
    * not it has been advertised. Since meta aims to only be restricted by the
    * driver capability (and not by whether or not an extension has been
    * advertised), set the helper functions' Version variable to a value that
    * will make the checks on the context API and version unconditionally pass.
    */
   save->ExtensionsVersion = ctx->Extensions.Version;
   ctx->Extensions.Version = ~0;

   /* Pausing transform feedback needs to be done early, or else we won't be
    * able to change other state.
    */
   save->TransformFeedbackNeedsResume =
      _mesa_is_xfb_active_and_unpaused(ctx);
   if (save->TransformFeedbackNeedsResume)
      _mesa_PauseTransformFeedback();

   /* After saving the current occlusion object, call EndQuery so that no
    * occlusion querying will be active during the meta-operation.
    */
   if (state & MESA_META_OCCLUSION_QUERY) {
      save->CurrentOcclusionObject = ctx->Query.CurrentOcclusionObject;
      if (save->CurrentOcclusionObject)
         _mesa_EndQuery(save->CurrentOcclusionObject->Target);
   }

   if (state & MESA_META_ALPHA_TEST) {
      save->AlphaEnabled = ctx->Color.AlphaEnabled;
      save->AlphaFunc = ctx->Color.AlphaFunc;
      save->AlphaRef = ctx->Color.AlphaRef;
      if (ctx->Color.AlphaEnabled)
         _mesa_set_enable(ctx, GL_ALPHA_TEST, GL_FALSE);
   }

   if (state & MESA_META_BLEND) {
      save->BlendEnabled = ctx->Color.BlendEnabled;
      if (ctx->Color.BlendEnabled) {
         if (ctx->Extensions.EXT_draw_buffers2) {
            GLuint i;
            for (i = 0; i < ctx->Const.MaxDrawBuffers; i++) {
               _mesa_set_enablei(ctx, GL_BLEND, i, GL_FALSE);
            }
         }
         else {
            _mesa_set_enable(ctx, GL_BLEND, GL_FALSE);
         }
      }
      save->ColorLogicOpEnabled = ctx->Color.ColorLogicOpEnabled;
      if (ctx->Color.ColorLogicOpEnabled)
         _mesa_set_enable(ctx, GL_COLOR_LOGIC_OP, GL_FALSE);
   }

   if (state & MESA_META_DITHER) {
      save->DitherFlag = ctx->Color.DitherFlag;
      _mesa_set_enable(ctx, GL_DITHER, GL_TRUE);
   }

   if (state & MESA_META_COLOR_MASK) {
      memcpy(save->ColorMask, ctx->Color.ColorMask,
             sizeof(ctx->Color.ColorMask));
   }

   if (state & MESA_META_DEPTH_TEST) {
      save->Depth = ctx->Depth; /* struct copy */
      if (ctx->Depth.Test)
         _mesa_set_enable(ctx, GL_DEPTH_TEST, GL_FALSE);
   }

   if (state & MESA_META_FOG) {
      save->Fog = ctx->Fog.Enabled;
      if (ctx->Fog.Enabled)
         _mesa_set_enable(ctx, GL_FOG, GL_FALSE);
   }

   if (state & MESA_META_PIXEL_STORE) {
      save->Pack = ctx->Pack;
      save->Unpack = ctx->Unpack;
      ctx->Pack = ctx->DefaultPacking;
      ctx->Unpack = ctx->DefaultPacking;
   }

   if (state & MESA_META_PIXEL_TRANSFER) {
      save->RedScale = ctx->Pixel.RedScale;
      save->RedBias = ctx->Pixel.RedBias;
      save->GreenScale = ctx->Pixel.GreenScale;
      save->GreenBias = ctx->Pixel.GreenBias;
      save->BlueScale = ctx->Pixel.BlueScale;
      save->BlueBias = ctx->Pixel.BlueBias;
      save->AlphaScale = ctx->Pixel.AlphaScale;
      save->AlphaBias = ctx->Pixel.AlphaBias;
      save->MapColorFlag = ctx->Pixel.MapColorFlag;
      ctx->Pixel.RedScale = 1.0F;
      ctx->Pixel.RedBias = 0.0F;
      ctx->Pixel.GreenScale = 1.0F;
      ctx->Pixel.GreenBias = 0.0F;
      ctx->Pixel.BlueScale = 1.0F;
      ctx->Pixel.BlueBias = 0.0F;
      ctx->Pixel.AlphaScale = 1.0F;
      ctx->Pixel.AlphaBias = 0.0F;
      ctx->Pixel.MapColorFlag = GL_FALSE;
      /* XXX more state */
      ctx->NewState |=_NEW_PIXEL;
   }

   if (state & MESA_META_RASTERIZATION) {
      save->FrontPolygonMode = ctx->Polygon.FrontMode;
      save->BackPolygonMode = ctx->Polygon.BackMode;
      save->PolygonOffset = ctx->Polygon.OffsetFill;
      save->PolygonSmooth = ctx->Polygon.SmoothFlag;
      save->PolygonStipple = ctx->Polygon.StippleFlag;
      save->PolygonCull = ctx->Polygon.CullFlag;
      _mesa_PolygonMode(GL_FRONT_AND_BACK, GL_FILL);
      _mesa_set_enable(ctx, GL_POLYGON_OFFSET_FILL, GL_FALSE);
      _mesa_set_enable(ctx, GL_POLYGON_SMOOTH, GL_FALSE);
      _mesa_set_enable(ctx, GL_POLYGON_STIPPLE, GL_FALSE);
      _mesa_set_enable(ctx, GL_CULL_FACE, GL_FALSE);
   }

   if (state & MESA_META_SCISSOR) {
      save->Scissor = ctx->Scissor; /* struct copy */
      _mesa_set_enable(ctx, GL_SCISSOR_TEST, GL_FALSE);
   }

   if (state & MESA_META_SHADER) {
      int i;

      if (ctx->Extensions.ARB_vertex_program) {
         save->VertexProgramEnabled = ctx->VertexProgram.Enabled;
         _mesa_reference_program(ctx, &save->VertexProgram,
                                 ctx->VertexProgram.Current);
         _mesa_set_enable(ctx, GL_VERTEX_PROGRAM_ARB, GL_FALSE);
      }

      if (ctx->Extensions.ARB_fragment_program) {
         save->FragmentProgramEnabled = ctx->FragmentProgram.Enabled;
         _mesa_reference_program(ctx, &save->FragmentProgram,
                                 ctx->FragmentProgram.Current);
         _mesa_set_enable(ctx, GL_FRAGMENT_PROGRAM_ARB, GL_FALSE);
      }

      if (ctx->Extensions.ATI_fragment_shader) {
         save->ATIFragmentShaderEnabled = ctx->ATIFragmentShader.Enabled;
         _mesa_set_enable(ctx, GL_FRAGMENT_SHADER_ATI, GL_FALSE);
      }

      if (ctx->Pipeline.Current) {
         _mesa_reference_pipeline_object(ctx, &save->Pipeline,
                                         ctx->Pipeline.Current);
         _mesa_BindProgramPipeline(0);
      }

      /* Save the shader state from ctx->Shader (instead of ctx->_Shader) so
       * that we don't have to worry about the current pipeline state.
       */
      for (i = 0; i < MESA_SHADER_STAGES; i++) {
         _mesa_reference_shader_program(ctx, &save->Shader[i],
                                        ctx->Shader.CurrentProgram[i]);
      }
      _mesa_reference_shader_program(ctx, &save->ActiveShader,
                                     ctx->Shader.ActiveProgram);

      _mesa_UseProgram(0);
   }

   if (state & MESA_META_STENCIL_TEST) {
      save->Stencil = ctx->Stencil; /* struct copy */
      if (ctx->Stencil.Enabled)
         _mesa_set_enable(ctx, GL_STENCIL_TEST, GL_FALSE);
      /* NOTE: other stencil state not reset */
   }

   if (state & MESA_META_TEXTURE) {
      GLuint u, tgt;

      save->ActiveUnit = ctx->Texture.CurrentUnit;
      save->EnvMode = ctx->Texture.Unit[0].EnvMode;

      /* Disable all texture units */
      for (u = 0; u < ctx->Const.MaxTextureUnits; u++) {
         save->TexEnabled[u] = ctx->Texture.Unit[u].Enabled;
         save->TexGenEnabled[u] = ctx->Texture.Unit[u].TexGenEnabled;
         if (ctx->Texture.Unit[u].Enabled ||
             ctx->Texture.Unit[u].TexGenEnabled) {
            _mesa_ActiveTexture(GL_TEXTURE0 + u);
            _mesa_set_enable(ctx, GL_TEXTURE_2D, GL_FALSE);
            if (ctx->Extensions.ARB_texture_cube_map)
               _mesa_set_enable(ctx, GL_TEXTURE_CUBE_MAP, GL_FALSE);

            _mesa_set_enable(ctx, GL_TEXTURE_1D, GL_FALSE);
            _mesa_set_enable(ctx, GL_TEXTURE_3D, GL_FALSE);
            if (ctx->Extensions.NV_texture_rectangle)
               _mesa_set_enable(ctx, GL_TEXTURE_RECTANGLE, GL_FALSE);
            _mesa_set_enable(ctx, GL_TEXTURE_GEN_S, GL_FALSE);
            _mesa_set_enable(ctx, GL_TEXTURE_GEN_T, GL_FALSE);
            _mesa_set_enable(ctx, GL_TEXTURE_GEN_R, GL_FALSE);
            _mesa_set_enable(ctx, GL_TEXTURE_GEN_Q, GL_FALSE);
         }
      }

      /* save current texture objects for unit[0] only */
      for (tgt = 0; tgt < NUM_TEXTURE_TARGETS; tgt++) {
         _mesa_reference_texobj(&save->CurrentTexture[tgt],
                                ctx->Texture.Unit[0].CurrentTex[tgt]);
      }

      /* set defaults for unit[0] */
      _mesa_ActiveTexture(GL_TEXTURE0);
      _mesa_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
   }

   if (state & MESA_META_TRANSFORM) {
      GLuint activeTexture = ctx->Texture.CurrentUnit;
      memcpy(save->ModelviewMatrix, ctx->ModelviewMatrixStack.Top->m,
             16 * sizeof(GLfloat));
      memcpy(save->ProjectionMatrix, ctx->ProjectionMatrixStack.Top->m,
             16 * sizeof(GLfloat));
      memcpy(save->TextureMatrix, ctx->TextureMatrixStack[0].Top->m,
             16 * sizeof(GLfloat));
      save->MatrixMode = ctx->Transform.MatrixMode;
      /* set 1:1 vertex:pixel coordinate transform */
      _mesa_ActiveTexture(GL_TEXTURE0);
      _mesa_MatrixMode(GL_TEXTURE);
      _mesa_LoadIdentity();
      _mesa_ActiveTexture(GL_TEXTURE0 + activeTexture);
      _mesa_MatrixMode(GL_MODELVIEW);
      _mesa_LoadIdentity();
      _mesa_MatrixMode(GL_PROJECTION);
      _mesa_LoadIdentity();

      /* glOrtho with width = 0 or height = 0 generates GL_INVALID_VALUE.
       * This can occur when there is no draw buffer.
       */
      if (ctx->DrawBuffer->Width != 0 && ctx->DrawBuffer->Height != 0)
         _mesa_Ortho(0.0, ctx->DrawBuffer->Width,
                     0.0, ctx->DrawBuffer->Height,
                     -1.0, 1.0);

      if (ctx->Extensions.ARB_clip_control) {
         save->ClipOrigin = ctx->Transform.ClipOrigin;
         save->ClipDepthMode = ctx->Transform.ClipDepthMode;
         _mesa_ClipControl(GL_LOWER_LEFT, GL_NEGATIVE_ONE_TO_ONE);
      }
   }

   if (state & MESA_META_CLIP) {
      GLbitfield mask;
      save->ClipPlanesEnabled = ctx->Transform.ClipPlanesEnabled;
      mask = ctx->Transform.ClipPlanesEnabled;
      while (mask) {
         const int i = u_bit_scan(&mask);
         _mesa_set_enable(ctx, GL_CLIP_PLANE0 + i, GL_FALSE);
      }
   }

   if (state & MESA_META_VERTEX) {
      /* save vertex array object state */
      _mesa_reference_vao(ctx, &save->VAO,
                                   ctx->Array.VAO);
      /* set some default state? */
   }

   if (state & MESA_META_VIEWPORT) {
      /* save viewport state */
      save->ViewportX = ctx->ViewportArray[0].X;
      save->ViewportY = ctx->ViewportArray[0].Y;
      save->ViewportW = ctx->ViewportArray[0].Width;
      save->ViewportH = ctx->ViewportArray[0].Height;
      /* set viewport to match window size */
      if (ctx->ViewportArray[0].X != 0 ||
          ctx->ViewportArray[0].Y != 0 ||
          ctx->ViewportArray[0].Width != (float) ctx->DrawBuffer->Width ||
          ctx->ViewportArray[0].Height != (float) ctx->DrawBuffer->Height) {
         _mesa_set_viewport(ctx, 0, 0, 0,
                            ctx->DrawBuffer->Width, ctx->DrawBuffer->Height);
      }
      /* save depth range state */
      save->DepthNear = ctx->ViewportArray[0].Near;
      save->DepthFar = ctx->ViewportArray[0].Far;
      /* set depth range to default */
      _mesa_set_depth_range(ctx, 0, 0.0, 1.0);
   }

   if (state & MESA_META_CLAMP_FRAGMENT_COLOR) {
      save->ClampFragmentColor = ctx->Color.ClampFragmentColor;

      /* Generally in here we want to do clamping according to whether
       * it's for the pixel path (ClampFragmentColor is GL_TRUE),
       * regardless of the internal implementation of the metaops.
       */
      if (ctx->Color.ClampFragmentColor != GL_TRUE &&
          ctx->Extensions.ARB_color_buffer_float)
         _mesa_ClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);
   }

   if (state & MESA_META_CLAMP_VERTEX_COLOR) {
      save->ClampVertexColor = ctx->Light.ClampVertexColor;

      /* Generally in here we never want vertex color clamping --
       * result clamping is only dependent on fragment clamping.
       */
      if (ctx->Extensions.ARB_color_buffer_float)
         _mesa_ClampColor(GL_CLAMP_VERTEX_COLOR, GL_FALSE);
   }

   if (state & MESA_META_CONDITIONAL_RENDER) {
      save->CondRenderQuery = ctx->Query.CondRenderQuery;
      save->CondRenderMode = ctx->Query.CondRenderMode;

      if (ctx->Query.CondRenderQuery)
         _mesa_EndConditionalRender();
   }

   if (state & MESA_META_SELECT_FEEDBACK) {
      save->RenderMode = ctx->RenderMode;
      if (ctx->RenderMode == GL_SELECT) {
         save->Select = ctx->Select; /* struct copy */
         _mesa_RenderMode(GL_RENDER);
      } else if (ctx->RenderMode == GL_FEEDBACK) {
         save->Feedback = ctx->Feedback; /* struct copy */
         _mesa_RenderMode(GL_RENDER);
      }
   }

   if (state & MESA_META_MULTISAMPLE) {
      save->Multisample = ctx->Multisample; /* struct copy */

      if (ctx->Multisample.Enabled)
         _mesa_set_multisample(ctx, GL_FALSE);
      if (ctx->Multisample.SampleCoverage)
         _mesa_set_enable(ctx, GL_SAMPLE_COVERAGE, GL_FALSE);
      if (ctx->Multisample.SampleAlphaToCoverage)
         _mesa_set_enable(ctx, GL_SAMPLE_ALPHA_TO_COVERAGE, GL_FALSE);
      if (ctx->Multisample.SampleAlphaToOne)
         _mesa_set_enable(ctx, GL_SAMPLE_ALPHA_TO_ONE, GL_FALSE);
      if (ctx->Multisample.SampleShading)
         _mesa_set_enable(ctx, GL_SAMPLE_SHADING, GL_FALSE);
      if (ctx->Multisample.SampleMask)
         _mesa_set_enable(ctx, GL_SAMPLE_MASK, GL_FALSE);
   }

   if (state & MESA_META_FRAMEBUFFER_SRGB) {
      save->sRGBEnabled = ctx->Color.sRGBEnabled;
      if (ctx->Color.sRGBEnabled)
         _mesa_set_framebuffer_srgb(ctx, GL_FALSE);
   }

   if (state & MESA_META_DRAW_BUFFERS) {
      struct gl_framebuffer *fb = ctx->DrawBuffer;
      memcpy(save->ColorDrawBuffers, fb->ColorDrawBuffer,
             sizeof(save->ColorDrawBuffers));
   }

   /* misc */
   {
      save->Lighting = ctx->Light.Enabled;
      if (ctx->Light.Enabled)
         _mesa_set_enable(ctx, GL_LIGHTING, GL_FALSE);
      save->RasterDiscard = ctx->RasterDiscard;
      if (ctx->RasterDiscard)
         _mesa_set_enable(ctx, GL_RASTERIZER_DISCARD, GL_FALSE);

      _mesa_reference_framebuffer(&save->DrawBuffer, ctx->DrawBuffer);
      _mesa_reference_framebuffer(&save->ReadBuffer, ctx->ReadBuffer);
   }
}


/**
 * Leave meta state.  This is like a light-weight version of glPopAttrib().
 */
void
_mesa_meta_end(struct gl_context *ctx)
{
   assert(ctx->Meta->SaveStackDepth > 0);

   struct save_state *save = &ctx->Meta->Save[ctx->Meta->SaveStackDepth - 1];
   const GLbitfield state = save->SavedState;
   int i;

   /* Grab the result of the old occlusion query before starting it again. The
    * old result is added to the result of the new query so the driver will
    * continue adding where it left off. */
   if (state & MESA_META_OCCLUSION_QUERY) {
      if (save->CurrentOcclusionObject) {
         struct gl_query_object *q = save->CurrentOcclusionObject;
         GLuint64EXT result;
         if (!q->Ready)
            ctx->Driver.WaitQuery(ctx, q);
         result = q->Result;
         _mesa_BeginQuery(q->Target, q->Id);
         ctx->Query.CurrentOcclusionObject->Result += result;
      }
   }

   if (state & MESA_META_ALPHA_TEST) {
      if (ctx->Color.AlphaEnabled != save->AlphaEnabled)
         _mesa_set_enable(ctx, GL_ALPHA_TEST, save->AlphaEnabled);
      _mesa_AlphaFunc(save->AlphaFunc, save->AlphaRef);
   }

   if (state & MESA_META_BLEND) {
      if (ctx->Color.BlendEnabled != save->BlendEnabled) {
         if (ctx->Extensions.EXT_draw_buffers2) {
            GLuint i;
            for (i = 0; i < ctx->Const.MaxDrawBuffers; i++) {
               _mesa_set_enablei(ctx, GL_BLEND, i, (save->BlendEnabled >> i) & 1);
            }
         }
         else {
            _mesa_set_enable(ctx, GL_BLEND, (save->BlendEnabled & 1));
         }
      }
      if (ctx->Color.ColorLogicOpEnabled != save->ColorLogicOpEnabled)
         _mesa_set_enable(ctx, GL_COLOR_LOGIC_OP, save->ColorLogicOpEnabled);
   }

   if (state & MESA_META_DITHER)
      _mesa_set_enable(ctx, GL_DITHER, save->DitherFlag);

   if (state & MESA_META_COLOR_MASK) {
      GLuint i;
      for (i = 0; i < ctx->Const.MaxDrawBuffers; i++) {
         if (!TEST_EQ_4V(ctx->Color.ColorMask[i], save->ColorMask[i])) {
            if (i == 0) {
               _mesa_ColorMask(save->ColorMask[i][0], save->ColorMask[i][1],
                               save->ColorMask[i][2], save->ColorMask[i][3]);
            }
            else {
               _mesa_ColorMaski(i,
                                      save->ColorMask[i][0],
                                      save->ColorMask[i][1],
                                      save->ColorMask[i][2],
                                      save->ColorMask[i][3]);
            }
         }
      }
   }

   if (state & MESA_META_DEPTH_TEST) {
      if (ctx->Depth.Test != save->Depth.Test)
         _mesa_set_enable(ctx, GL_DEPTH_TEST, save->Depth.Test);
      _mesa_DepthFunc(save->Depth.Func);
      _mesa_DepthMask(save->Depth.Mask);
   }

   if (state & MESA_META_FOG) {
      _mesa_set_enable(ctx, GL_FOG, save->Fog);
   }

   if (state & MESA_META_PIXEL_STORE) {
      ctx->Pack = save->Pack;
      ctx->Unpack = save->Unpack;
   }

   if (state & MESA_META_PIXEL_TRANSFER) {
      ctx->Pixel.RedScale = save->RedScale;
      ctx->Pixel.RedBias = save->RedBias;
      ctx->Pixel.GreenScale = save->GreenScale;
      ctx->Pixel.GreenBias = save->GreenBias;
      ctx->Pixel.BlueScale = save->BlueScale;
      ctx->Pixel.BlueBias = save->BlueBias;
      ctx->Pixel.AlphaScale = save->AlphaScale;
      ctx->Pixel.AlphaBias = save->AlphaBias;
      ctx->Pixel.MapColorFlag = save->MapColorFlag;
      /* XXX more state */
      ctx->NewState |=_NEW_PIXEL;
   }

   if (state & MESA_META_RASTERIZATION) {
      _mesa_PolygonMode(GL_FRONT, save->FrontPolygonMode);
      _mesa_PolygonMode(GL_BACK, save->BackPolygonMode);
      _mesa_set_enable(ctx, GL_POLYGON_STIPPLE, save->PolygonStipple);
      _mesa_set_enable(ctx, GL_POLYGON_SMOOTH, save->PolygonSmooth);
      _mesa_set_enable(ctx, GL_POLYGON_OFFSET_FILL, save->PolygonOffset);
      _mesa_set_enable(ctx, GL_CULL_FACE, save->PolygonCull);
   }

   if (state & MESA_META_SCISSOR) {
      unsigned i;

      for (i = 0; i < ctx->Const.MaxViewports; i++) {
         _mesa_set_scissor(ctx, i,
                           save->Scissor.ScissorArray[i].X,
                           save->Scissor.ScissorArray[i].Y,
                           save->Scissor.ScissorArray[i].Width,
                           save->Scissor.ScissorArray[i].Height);
         _mesa_set_enablei(ctx, GL_SCISSOR_TEST, i,
                           (save->Scissor.EnableFlags >> i) & 1);
      }
   }

   if (state & MESA_META_SHADER) {
      static const GLenum targets[] = {
         GL_VERTEX_SHADER,
         GL_TESS_CONTROL_SHADER,
         GL_TESS_EVALUATION_SHADER,
         GL_GEOMETRY_SHADER,
         GL_FRAGMENT_SHADER,
         GL_COMPUTE_SHADER,
      };
      STATIC_ASSERT(MESA_SHADER_STAGES == ARRAY_SIZE(targets));

      bool any_shader;

      if (ctx->Extensions.ARB_vertex_program) {
         _mesa_set_enable(ctx, GL_VERTEX_PROGRAM_ARB,
                          save->VertexProgramEnabled);
         _mesa_reference_program(ctx, &ctx->VertexProgram.Current,
                                 save->VertexProgram);
         _mesa_reference_program(ctx, &save->VertexProgram, NULL);
      }

      if (ctx->Extensions.ARB_fragment_program) {
         _mesa_set_enable(ctx, GL_FRAGMENT_PROGRAM_ARB,
                          save->FragmentProgramEnabled);
         _mesa_reference_program(ctx, &ctx->FragmentProgram.Current,
                                 save->FragmentProgram);
         _mesa_reference_program(ctx, &save->FragmentProgram, NULL);
      }

      if (ctx->Extensions.ATI_fragment_shader) {
         _mesa_set_enable(ctx, GL_FRAGMENT_SHADER_ATI,
                          save->ATIFragmentShaderEnabled);
      }

      any_shader = false;
      for (i = 0; i < MESA_SHADER_STAGES; i++) {
         /* It is safe to call _mesa_use_shader_program even if the extension
          * necessary for that program state is not supported.  In that case,
          * the saved program object must be NULL and the currently bound
          * program object must be NULL.  _mesa_use_shader_program is a no-op
          * in that case.
          */
         _mesa_use_shader_program(ctx, targets[i],
                                  save->Shader[i],
                                  &ctx->Shader);

         /* Do this *before* killing the reference. :)
          */
         if (save->Shader[i] != NULL)
            any_shader = true;

         _mesa_reference_shader_program(ctx, &save->Shader[i], NULL);
      }

      _mesa_reference_shader_program(ctx, &ctx->Shader.ActiveProgram,
                                     save->ActiveShader);
      _mesa_reference_shader_program(ctx, &save->ActiveShader, NULL);

      /* If there were any stages set with programs, use ctx->Shader as the
       * current shader state.  Otherwise, use Pipeline.Default.  The pipeline
       * hasn't been restored yet, and that may modify ctx->_Shader further.
       */
      if (any_shader)
         _mesa_reference_pipeline_object(ctx, &ctx->_Shader,
                                         &ctx->Shader);
      else
         _mesa_reference_pipeline_object(ctx, &ctx->_Shader,
                                         ctx->Pipeline.Default);

      if (save->Pipeline) {
         _mesa_bind_pipeline(ctx, save->Pipeline);

         _mesa_reference_pipeline_object(ctx, &save->Pipeline, NULL);
      }
   }

   if (state & MESA_META_STENCIL_TEST) {
      const struct gl_stencil_attrib *stencil = &save->Stencil;

      _mesa_set_enable(ctx, GL_STENCIL_TEST, stencil->Enabled);
      _mesa_ClearStencil(stencil->Clear);
      if (ctx->Extensions.EXT_stencil_two_side) {
         _mesa_set_enable(ctx, GL_STENCIL_TEST_TWO_SIDE_EXT,
                          stencil->TestTwoSide);
         _mesa_ActiveStencilFaceEXT(stencil->ActiveFace
                                    ? GL_BACK : GL_FRONT);
      }
      /* front state */
      _mesa_StencilFuncSeparate(GL_FRONT,
                                stencil->Function[0],
                                stencil->Ref[0],
                                stencil->ValueMask[0]);
      _mesa_StencilMaskSeparate(GL_FRONT, stencil->WriteMask[0]);
      _mesa_StencilOpSeparate(GL_FRONT, stencil->FailFunc[0],
                              stencil->ZFailFunc[0],
                              stencil->ZPassFunc[0]);
      /* back state */
      _mesa_StencilFuncSeparate(GL_BACK,
                                stencil->Function[1],
                                stencil->Ref[1],
                                stencil->ValueMask[1]);
      _mesa_StencilMaskSeparate(GL_BACK, stencil->WriteMask[1]);
      _mesa_StencilOpSeparate(GL_BACK, stencil->FailFunc[1],
                              stencil->ZFailFunc[1],
                              stencil->ZPassFunc[1]);
   }

   if (state & MESA_META_TEXTURE) {
      GLuint u, tgt;

      assert(ctx->Texture.CurrentUnit == 0);

      /* restore texenv for unit[0] */
      _mesa_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, save->EnvMode);

      /* restore texture objects for unit[0] only */
      for (tgt = 0; tgt < NUM_TEXTURE_TARGETS; tgt++) {
         if (ctx->Texture.Unit[0].CurrentTex[tgt] != save->CurrentTexture[tgt]) {
            FLUSH_VERTICES(ctx, _NEW_TEXTURE);
            _mesa_reference_texobj(&ctx->Texture.Unit[0].CurrentTex[tgt],
                                   save->CurrentTexture[tgt]);
         }
         _mesa_reference_texobj(&save->CurrentTexture[tgt], NULL);
      }

      /* Restore fixed function texture enables, texgen */
      for (u = 0; u < ctx->Const.MaxTextureUnits; u++) {
         if (ctx->Texture.Unit[u].Enabled != save->TexEnabled[u]) {
            FLUSH_VERTICES(ctx, _NEW_TEXTURE);
            ctx->Texture.Unit[u].Enabled = save->TexEnabled[u];
         }

         if (ctx->Texture.Unit[u].TexGenEnabled != save->TexGenEnabled[u]) {
            FLUSH_VERTICES(ctx, _NEW_TEXTURE);
            ctx->Texture.Unit[u].TexGenEnabled = save->TexGenEnabled[u];
         }
      }

      /* restore current unit state */
      _mesa_ActiveTexture(GL_TEXTURE0 + save->ActiveUnit);
   }

   if (state & MESA_META_TRANSFORM) {
      GLuint activeTexture = ctx->Texture.CurrentUnit;
      _mesa_ActiveTexture(GL_TEXTURE0);
      _mesa_MatrixMode(GL_TEXTURE);
      _mesa_LoadMatrixf(save->TextureMatrix);
      _mesa_ActiveTexture(GL_TEXTURE0 + activeTexture);

      _mesa_MatrixMode(GL_MODELVIEW);
      _mesa_LoadMatrixf(save->ModelviewMatrix);

      _mesa_MatrixMode(GL_PROJECTION);
      _mesa_LoadMatrixf(save->ProjectionMatrix);

      _mesa_MatrixMode(save->MatrixMode);

      if (ctx->Extensions.ARB_clip_control)
         _mesa_ClipControl(save->ClipOrigin, save->ClipDepthMode);
   }

   if (state & MESA_META_CLIP) {
      GLbitfield mask = save->ClipPlanesEnabled;
      while (mask) {
         const int i = u_bit_scan(&mask);
         _mesa_set_enable(ctx, GL_CLIP_PLANE0 + i, GL_TRUE);
      }
   }

   if (state & MESA_META_VERTEX) {
      /* restore vertex array object */
      _mesa_BindVertexArray(save->VAO->Name);
      _mesa_reference_vao(ctx, &save->VAO, NULL);
   }

   if (state & MESA_META_VIEWPORT) {
      if (save->ViewportX != ctx->ViewportArray[0].X ||
          save->ViewportY != ctx->ViewportArray[0].Y ||
          save->ViewportW != ctx->ViewportArray[0].Width ||
          save->ViewportH != ctx->ViewportArray[0].Height) {
         _mesa_set_viewport(ctx, 0, save->ViewportX, save->ViewportY,
                            save->ViewportW, save->ViewportH);
      }
      _mesa_set_depth_range(ctx, 0, save->DepthNear, save->DepthFar);
   }

   if (state & MESA_META_CLAMP_FRAGMENT_COLOR &&
       ctx->Extensions.ARB_color_buffer_float) {
      _mesa_ClampColor(GL_CLAMP_FRAGMENT_COLOR, save->ClampFragmentColor);
   }

   if (state & MESA_META_CLAMP_VERTEX_COLOR &&
       ctx->Extensions.ARB_color_buffer_float) {
      _mesa_ClampColor(GL_CLAMP_VERTEX_COLOR, save->ClampVertexColor);
   }

   if (state & MESA_META_CONDITIONAL_RENDER) {
      if (save->CondRenderQuery)
         _mesa_BeginConditionalRender(save->CondRenderQuery->Id,
                                      save->CondRenderMode);
   }

   if (state & MESA_META_SELECT_FEEDBACK) {
      if (save->RenderMode == GL_SELECT) {
         _mesa_RenderMode(GL_SELECT);
         ctx->Select = save->Select;
      } else if (save->RenderMode == GL_FEEDBACK) {
         _mesa_RenderMode(GL_FEEDBACK);
         ctx->Feedback = save->Feedback;
      }
   }

   if (state & MESA_META_MULTISAMPLE) {
      struct gl_multisample_attrib *ctx_ms = &ctx->Multisample;
      struct gl_multisample_attrib *save_ms = &save->Multisample;

      if (ctx_ms->Enabled != save_ms->Enabled)
         _mesa_set_multisample(ctx, save_ms->Enabled);
      if (ctx_ms->SampleCoverage != save_ms->SampleCoverage)
         _mesa_set_enable(ctx, GL_SAMPLE_COVERAGE, save_ms->SampleCoverage);
      if (ctx_ms->SampleAlphaToCoverage != save_ms->SampleAlphaToCoverage)
         _mesa_set_enable(ctx, GL_SAMPLE_ALPHA_TO_COVERAGE, save_ms->SampleAlphaToCoverage);
      if (ctx_ms->SampleAlphaToOne != save_ms->SampleAlphaToOne)
         _mesa_set_enable(ctx, GL_SAMPLE_ALPHA_TO_ONE, save_ms->SampleAlphaToOne);
      if (ctx_ms->SampleCoverageValue != save_ms->SampleCoverageValue ||
          ctx_ms->SampleCoverageInvert != save_ms->SampleCoverageInvert) {
         _mesa_SampleCoverage(save_ms->SampleCoverageValue,
                              save_ms->SampleCoverageInvert);
      }
      if (ctx_ms->SampleShading != save_ms->SampleShading)
         _mesa_set_enable(ctx, GL_SAMPLE_SHADING, save_ms->SampleShading);
      if (ctx_ms->SampleMask != save_ms->SampleMask)
         _mesa_set_enable(ctx, GL_SAMPLE_MASK, save_ms->SampleMask);
      if (ctx_ms->SampleMaskValue != save_ms->SampleMaskValue)
         _mesa_SampleMaski(0, save_ms->SampleMaskValue);
      if (ctx_ms->MinSampleShadingValue != save_ms->MinSampleShadingValue)
         _mesa_MinSampleShading(save_ms->MinSampleShadingValue);
   }

   if (state & MESA_META_FRAMEBUFFER_SRGB) {
      if (ctx->Color.sRGBEnabled != save->sRGBEnabled)
         _mesa_set_framebuffer_srgb(ctx, save->sRGBEnabled);
   }

   /* misc */
   if (save->Lighting) {
      _mesa_set_enable(ctx, GL_LIGHTING, GL_TRUE);
   }
   if (save->RasterDiscard) {
      _mesa_set_enable(ctx, GL_RASTERIZER_DISCARD, GL_TRUE);
   }
   if (save->TransformFeedbackNeedsResume)
      _mesa_ResumeTransformFeedback();

   _mesa_bind_framebuffers(ctx, save->DrawBuffer, save->ReadBuffer);
   _mesa_reference_framebuffer(&save->DrawBuffer, NULL);
   _mesa_reference_framebuffer(&save->ReadBuffer, NULL);

   if (state & MESA_META_DRAW_BUFFERS) {
      _mesa_drawbuffers(ctx, ctx->DrawBuffer, ctx->Const.MaxDrawBuffers,
                        save->ColorDrawBuffers, NULL);
   }

   ctx->Meta->SaveStackDepth--;

   ctx->API = save->API;
   ctx->Extensions.Version = save->ExtensionsVersion;
}


/**
 * Convert Z from a normalized value in the range [0, 1] to an object-space
 * Z coordinate in [-1, +1] so that drawing at the new Z position with the
 * default/identity ortho projection results in the original Z value.
 * Used by the meta-Clear, Draw/CopyPixels and Bitmap functions where the Z
 * value comes from the clear value or raster position.
 */
static inline GLfloat
invert_z(GLfloat normZ)
{
   GLfloat objZ = 1.0f - 2.0f * normZ;
   return objZ;
}


/**
 * One-time init for a temp_texture object.
 * Choose tex target, compute max tex size, etc.
 */
static void
init_temp_texture(struct gl_context *ctx, struct temp_texture *tex)
{
   /* prefer texture rectangle */
   if (_mesa_is_desktop_gl(ctx) && ctx->Extensions.NV_texture_rectangle) {
      tex->Target = GL_TEXTURE_RECTANGLE;
      tex->MaxSize = ctx->Const.MaxTextureRectSize;
      tex->NPOT = GL_TRUE;
   }
   else {
      /* use 2D texture, NPOT if possible */
      tex->Target = GL_TEXTURE_2D;
      tex->MaxSize = 1 << (ctx->Const.MaxTextureLevels - 1);
      tex->NPOT = ctx->Extensions.ARB_texture_non_power_of_two;
   }
   tex->MinSize = 16;  /* 16 x 16 at least */
   assert(tex->MaxSize > 0);

   _mesa_GenTextures(1, &tex->TexObj);
}

static void
cleanup_temp_texture(struct temp_texture *tex)
{
   if (!tex->TexObj)
     return;
   _mesa_DeleteTextures(1, &tex->TexObj);
   tex->TexObj = 0;
}


/**
 * Return pointer to temp_texture info for non-bitmap ops.
 * This does some one-time init if needed.
 */
struct temp_texture *
_mesa_meta_get_temp_texture(struct gl_context *ctx)
{
   struct temp_texture *tex = &ctx->Meta->TempTex;

   if (!tex->TexObj) {
      init_temp_texture(ctx, tex);
   }

   return tex;
}


/**
 * Return pointer to temp_texture info for _mesa_meta_bitmap().
 * We use a separate texture for bitmaps to reduce texture
 * allocation/deallocation.
 */
static struct temp_texture *
get_bitmap_temp_texture(struct gl_context *ctx)
{
   struct temp_texture *tex = &ctx->Meta->Bitmap.Tex;

   if (!tex->TexObj) {
      init_temp_texture(ctx, tex);
   }

   return tex;
}

/**
 * Return pointer to depth temp_texture.
 * This does some one-time init if needed.
 */
struct temp_texture *
_mesa_meta_get_temp_depth_texture(struct gl_context *ctx)
{
   struct temp_texture *tex = &ctx->Meta->Blit.depthTex;

   if (!tex->TexObj) {
      init_temp_texture(ctx, tex);
   }

   return tex;
}

/**
 * Compute the width/height of texture needed to draw an image of the
 * given size.  Return a flag indicating whether the current texture
 * can be re-used (glTexSubImage2D) or if a new texture needs to be
 * allocated (glTexImage2D).
 * Also, compute s/t texcoords for drawing.
 *
 * \return GL_TRUE if new texture is needed, GL_FALSE otherwise
 */
GLboolean
_mesa_meta_alloc_texture(struct temp_texture *tex,
                         GLsizei width, GLsizei height, GLenum intFormat)
{
   GLboolean newTex = GL_FALSE;

   assert(width <= tex->MaxSize);
   assert(height <= tex->MaxSize);

   if (width > tex->Width ||
       height > tex->Height ||
       intFormat != tex->IntFormat) {
      /* alloc new texture (larger or different format) */

      if (tex->NPOT) {
         /* use non-power of two size */
         tex->Width = MAX2(tex->MinSize, width);
         tex->Height = MAX2(tex->MinSize, height);
      }
      else {
         /* find power of two size */
         GLsizei w, h;
         w = h = tex->MinSize;
         while (w < width)
            w *= 2;
         while (h < height)
            h *= 2;
         tex->Width = w;
         tex->Height = h;
      }

      tex->IntFormat = intFormat;

      newTex = GL_TRUE;
   }

   /* compute texcoords */
   if (tex->Target == GL_TEXTURE_RECTANGLE) {
      tex->Sright = (GLfloat) width;
      tex->Ttop = (GLfloat) height;
   }
   else {
      tex->Sright = (GLfloat) width / tex->Width;
      tex->Ttop = (GLfloat) height / tex->Height;
   }

   return newTex;
}


/**
 * Setup/load texture for glCopyPixels or glBlitFramebuffer.
 */
void
_mesa_meta_setup_copypix_texture(struct gl_context *ctx,
                                 struct temp_texture *tex,
                                 GLint srcX, GLint srcY,
                                 GLsizei width, GLsizei height,
                                 GLenum intFormat,
                                 GLenum filter)
{
   bool newTex;

   _mesa_BindTexture(tex->Target, tex->TexObj);
   _mesa_TexParameteri(tex->Target, GL_TEXTURE_MIN_FILTER, filter);
   _mesa_TexParameteri(tex->Target, GL_TEXTURE_MAG_FILTER, filter);
   _mesa_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

   newTex = _mesa_meta_alloc_texture(tex, width, height, intFormat);

   /* copy framebuffer image to texture */
   if (newTex) {
      /* create new tex image */
      if (tex->Width == width && tex->Height == height) {
         /* create new tex with framebuffer data */
         _mesa_CopyTexImage2D(tex->Target, 0, tex->IntFormat,
                              srcX, srcY, width, height, 0);
      }
      else {
         /* create empty texture */
         _mesa_TexImage2D(tex->Target, 0, tex->IntFormat,
                          tex->Width, tex->Height, 0,
                          intFormat, GL_UNSIGNED_BYTE, NULL);
         /* load image */
         _mesa_CopyTexSubImage2D(tex->Target, 0,
                                 0, 0, srcX, srcY, width, height);
      }
   }
   else {
      /* replace existing tex image */
      _mesa_CopyTexSubImage2D(tex->Target, 0,
                              0, 0, srcX, srcY, width, height);
   }
}


/**
 * Setup/load texture for glDrawPixels.
 */
void
_mesa_meta_setup_drawpix_texture(struct gl_context *ctx,
                                 struct temp_texture *tex,
                                 GLboolean newTex,
                                 GLsizei width, GLsizei height,
                                 GLenum format, GLenum type,
                                 const GLvoid *pixels)
{
   _mesa_BindTexture(tex->Target, tex->TexObj);
   _mesa_TexParameteri(tex->Target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
   _mesa_TexParameteri(tex->Target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
   _mesa_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

   /* copy pixel data to texture */
   if (newTex) {
      /* create new tex image */
      if (tex->Width == width && tex->Height == height) {
         /* create new tex and load image data */
         _mesa_TexImage2D(tex->Target, 0, tex->IntFormat,
                          tex->Width, tex->Height, 0, format, type, pixels);
      }
      else {
         struct gl_buffer_object *save_unpack_obj = NULL;

         _mesa_reference_buffer_object(ctx, &save_unpack_obj,
                                       ctx->Unpack.BufferObj);
         _mesa_BindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB, 0);
         /* create empty texture */
         _mesa_TexImage2D(tex->Target, 0, tex->IntFormat,
                          tex->Width, tex->Height, 0, format, type, NULL);
         if (save_unpack_obj != NULL)
            _mesa_BindBuffer(GL_PIXEL_UNPACK_BUFFER_ARB,
                             save_unpack_obj->Name);
         /* load image */
         _mesa_TexSubImage2D(tex->Target, 0,
                             0, 0, width, height, format, type, pixels);
      }
   }
   else {
      /* replace existing tex image */
      _mesa_TexSubImage2D(tex->Target, 0,
                          0, 0, width, height, format, type, pixels);
   }
}

void
_mesa_meta_setup_ff_tnl_for_blit(struct gl_context *ctx,
                                 GLuint *VAO, struct gl_buffer_object **buf_obj,
                                 unsigned texcoord_size)
{
   _mesa_meta_setup_vertex_objects(ctx, VAO, buf_obj, false, 2, texcoord_size,
                                   0);

   /* setup projection matrix */
   _mesa_MatrixMode(GL_PROJECTION);
   _mesa_LoadIdentity();
}

/**
 * Meta implementation of ctx->Driver.Clear() in terms of polygon rendering.
 */
void
_mesa_meta_Clear(struct gl_context *ctx, GLbitfield buffers)
{
   meta_clear(ctx, buffers, false);
}

void
_mesa_meta_glsl_Clear(struct gl_context *ctx, GLbitfield buffers)
{
   meta_clear(ctx, buffers, true);
}

static void
meta_glsl_clear_init(struct gl_context *ctx, struct clear_state *clear)
{
   const char *vs_source =
      "#extension GL_AMD_vertex_shader_layer : enable\n"
      "#extension GL_ARB_draw_instanced : enable\n"
      "#extension GL_ARB_explicit_attrib_location :enable\n"
      "layout(location = 0) in vec4 position;\n"
      "void main()\n"
      "{\n"
      "#ifdef GL_AMD_vertex_shader_layer\n"
      "   gl_Layer = gl_InstanceID;\n"
      "#endif\n"
      "   gl_Position = position;\n"
      "}\n";
   const char *fs_source =
      "#extension GL_ARB_explicit_attrib_location :enable\n"
      "#extension GL_ARB_explicit_uniform_location :enable\n"
      "layout(location = 0) uniform vec4 color;\n"
      "void main()\n"
      "{\n"
      "   gl_FragColor = color;\n"
      "}\n";
   bool has_integer_textures;

   _mesa_meta_setup_vertex_objects(ctx, &clear->VAO, &clear->buf_obj, true,
                                   3, 0, 0);

   if (clear->ShaderProg != 0)
      return;

   _mesa_meta_compile_and_link_program(ctx, vs_source, fs_source, "meta clear",
                                       &clear->ShaderProg);

   has_integer_textures = _mesa_is_gles3(ctx) ||
      (_mesa_is_desktop_gl(ctx) && ctx->Const.GLSLVersion >= 130);

   if (has_integer_textures) {
      void *shader_source_mem_ctx = ralloc_context(NULL);
      const char *vs_int_source =
         ralloc_asprintf(shader_source_mem_ctx,
                         "#version 130\n"
                         "#extension GL_AMD_vertex_shader_layer : enable\n"
                         "#extension GL_ARB_draw_instanced : enable\n"
                         "#extension GL_ARB_explicit_attrib_location :enable\n"
                         "layout(location = 0) in vec4 position;\n"
                         "void main()\n"
                         "{\n"
                         "#ifdef GL_AMD_vertex_shader_layer\n"
                         "   gl_Layer = gl_InstanceID;\n"
                         "#endif\n"
                         "   gl_Position = position;\n"
                         "}\n");
      const char *fs_int_source =
         ralloc_asprintf(shader_source_mem_ctx,
                         "#version 130\n"
                         "#extension GL_ARB_explicit_attrib_location :enable\n"
                         "#extension GL_ARB_explicit_uniform_location :enable\n"
                         "layout(location = 0) uniform ivec4 color;\n"
                         "out ivec4 out_color;\n"
                         "\n"
                         "void main()\n"
                         "{\n"
                         "   out_color = color;\n"
                         "}\n");

      _mesa_meta_compile_and_link_program(ctx, vs_int_source, fs_int_source,
                                          "integer clear",
                                          &clear->IntegerShaderProg);
      ralloc_free(shader_source_mem_ctx);

      /* Note that user-defined out attributes get automatically assigned
       * locations starting from 0, so we don't need to explicitly
       * BindFragDataLocation to 0.
       */
   }
}

static void
meta_glsl_clear_cleanup(struct gl_context *ctx, struct clear_state *clear)
{
   if (clear->VAO == 0)
      return;
   _mesa_DeleteVertexArrays(1, &clear->VAO);
   clear->VAO = 0;
   _mesa_reference_buffer_object(ctx, &clear->buf_obj, NULL);
   _mesa_reference_shader_program(ctx, &clear->ShaderProg, NULL);

   if (clear->IntegerShaderProg) {
      _mesa_reference_shader_program(ctx, &clear->IntegerShaderProg, NULL);
   }
}

/**
 * Given a bitfield of BUFFER_BIT_x draw buffers, call glDrawBuffers to
 * set GL to only draw to those buffers.
 *
 * Since the bitfield has no associated order, the assignment of draw buffer
 * indices to color attachment indices is rather arbitrary.
 */
void
_mesa_meta_drawbuffers_from_bitfield(GLbitfield bits)
{
   GLenum enums[MAX_DRAW_BUFFERS];
   int i = 0;
   int n;

   /* This function is only legal for color buffer bitfields. */
   assert((bits & ~BUFFER_BITS_COLOR) == 0);

   /* Make sure we don't overflow any arrays. */
   assert(_mesa_bitcount(bits) <= MAX_DRAW_BUFFERS);

   enums[0] = GL_NONE;

   if (bits & BUFFER_BIT_FRONT_LEFT)
      enums[i++] = GL_FRONT_LEFT;

   if (bits & BUFFER_BIT_FRONT_RIGHT)
      enums[i++] = GL_FRONT_RIGHT;

   if (bits & BUFFER_BIT_BACK_LEFT)
      enums[i++] = GL_BACK_LEFT;

   if (bits & BUFFER_BIT_BACK_RIGHT)
      enums[i++] = GL_BACK_RIGHT;

   for (n = 0; n < MAX_COLOR_ATTACHMENTS; n++) {
      if (bits & (1 << (BUFFER_COLOR0 + n)))
         enums[i++] = GL_COLOR_ATTACHMENT0 + n;
   }

   _mesa_DrawBuffers(i, enums);
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
 * Given a bitfield of BUFFER_BIT_x draw buffers, call glDrawBuffers to
 * set GL to only draw to those buffers.  Also, update color masks to
 * reflect the new draw buffer ordering.
 */
static void
_mesa_meta_drawbuffers_and_colormask(struct gl_context *ctx, GLbitfield mask)
{
   GLenum enums[MAX_DRAW_BUFFERS];
   GLubyte colormask[MAX_DRAW_BUFFERS][4];
   int num_bufs = 0;

   /* This function is only legal for color buffer bitfields. */
   assert((mask & ~BUFFER_BITS_COLOR) == 0);

   /* Make sure we don't overflow any arrays. */
   assert(_mesa_bitcount(mask) <= MAX_DRAW_BUFFERS);

   enums[0] = GL_NONE;

   for (int i = 0; i < ctx->DrawBuffer->_NumColorDrawBuffers; i++) {
      int b = ctx->DrawBuffer->_ColorDrawBufferIndexes[i];
      int colormask_idx = ctx->Extensions.EXT_draw_buffers2 ? i : 0;

      if (b < 0 || !(mask & (1 << b)) || is_color_disabled(ctx, colormask_idx))
         continue;

      switch (b) {
      case BUFFER_FRONT_LEFT:
         enums[num_bufs] = GL_FRONT_LEFT;
         break;
      case BUFFER_FRONT_RIGHT:
         enums[num_bufs] = GL_FRONT_RIGHT;
         break;
      case BUFFER_BACK_LEFT:
         enums[num_bufs] = GL_BACK_LEFT;
         break;
      case BUFFER_BACK_RIGHT:
         enums[num_bufs] = GL_BACK_RIGHT;
         break;
      default:
         assert(b >= BUFFER_COLOR0 && b <= BUFFER_COLOR7);
         enums[num_bufs] = GL_COLOR_ATTACHMENT0 + (b - BUFFER_COLOR0);
         break;
      }

      for (int k = 0; k < 4; k++)
         colormask[num_bufs][k] = ctx->Color.ColorMask[colormask_idx][k];

      num_bufs++;
   }

   _mesa_DrawBuffers(num_bufs, enums);

   for (int i = 0; i < num_bufs; i++) {
      _mesa_ColorMaski(i, colormask[i][0], colormask[i][1],
                          colormask[i][2], colormask[i][3]);
   }
}


/**
 * Meta implementation of ctx->Driver.Clear() in terms of polygon rendering.
 */
static void
meta_clear(struct gl_context *ctx, GLbitfield buffers, bool glsl)
{
   struct clear_state *clear = &ctx->Meta->Clear;
   GLbitfield metaSave;
   const GLuint stencilMax = (1 << ctx->DrawBuffer->Visual.stencilBits) - 1;
   struct gl_framebuffer *fb = ctx->DrawBuffer;
   float x0, y0, x1, y1, z;
   struct vertex verts[4];
   int i;

   metaSave = (MESA_META_ALPHA_TEST |
               MESA_META_BLEND |
               MESA_META_COLOR_MASK |
               MESA_META_DEPTH_TEST |
               MESA_META_RASTERIZATION |
               MESA_META_SHADER |
               MESA_META_STENCIL_TEST |
               MESA_META_VERTEX |
               MESA_META_VIEWPORT |
               MESA_META_CLIP |
               MESA_META_CLAMP_FRAGMENT_COLOR |
               MESA_META_MULTISAMPLE |
               MESA_META_OCCLUSION_QUERY);

   if (!glsl) {
      metaSave |= MESA_META_FOG |
                  MESA_META_PIXEL_TRANSFER |
                  MESA_META_TRANSFORM |
                  MESA_META_TEXTURE |
                  MESA_META_CLAMP_VERTEX_COLOR |
                  MESA_META_SELECT_FEEDBACK;
   }

   if (buffers & BUFFER_BITS_COLOR) {
      metaSave |= MESA_META_DRAW_BUFFERS;
   }

   _mesa_meta_begin(ctx, metaSave);

   if (glsl) {
      meta_glsl_clear_init(ctx, clear);

      x0 = ((float) fb->_Xmin / fb->Width)  * 2.0f - 1.0f;
      y0 = ((float) fb->_Ymin / fb->Height) * 2.0f - 1.0f;
      x1 = ((float) fb->_Xmax / fb->Width)  * 2.0f - 1.0f;
      y1 = ((float) fb->_Ymax / fb->Height) * 2.0f - 1.0f;
      z = -invert_z(ctx->Depth.Clear);
   } else {
      _mesa_meta_setup_vertex_objects(ctx, &clear->VAO, &clear->buf_obj, false,
                                      3, 0, 4);

      x0 = (float) fb->_Xmin;
      y0 = (float) fb->_Ymin;
      x1 = (float) fb->_Xmax;
      y1 = (float) fb->_Ymax;
      z = invert_z(ctx->Depth.Clear);
   }

   if (fb->_IntegerBuffers) {
      assert(glsl);
      _mesa_meta_use_program(ctx, clear->IntegerShaderProg);
      _mesa_Uniform4iv(0, 1, ctx->Color.ClearColor.i);
   } else if (glsl) {
      _mesa_meta_use_program(ctx, clear->ShaderProg);
      _mesa_Uniform4fv(0, 1, ctx->Color.ClearColor.f);
   }

   /* GL_COLOR_BUFFER_BIT */
   if (buffers & BUFFER_BITS_COLOR) {
      /* Only draw to the buffers we were asked to clear. */
      _mesa_meta_drawbuffers_and_colormask(ctx, buffers & BUFFER_BITS_COLOR);

      /* leave colormask state as-is */

      /* Clears never have the color clamped. */
      if (ctx->Extensions.ARB_color_buffer_float)
         _mesa_ClampColor(GL_CLAMP_FRAGMENT_COLOR, GL_FALSE);
   }
   else {
      _mesa_ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
   }

   /* GL_DEPTH_BUFFER_BIT */
   if (buffers & BUFFER_BIT_DEPTH) {
      _mesa_set_enable(ctx, GL_DEPTH_TEST, GL_TRUE);
      _mesa_DepthFunc(GL_ALWAYS);
      _mesa_DepthMask(GL_TRUE);
   }
   else {
      assert(!ctx->Depth.Test);
   }

   /* GL_STENCIL_BUFFER_BIT */
   if (buffers & BUFFER_BIT_STENCIL) {
      _mesa_set_enable(ctx, GL_STENCIL_TEST, GL_TRUE);
      _mesa_StencilOpSeparate(GL_FRONT_AND_BACK,
                              GL_REPLACE, GL_REPLACE, GL_REPLACE);
      _mesa_StencilFuncSeparate(GL_FRONT_AND_BACK, GL_ALWAYS,
                                ctx->Stencil.Clear & stencilMax,
                                ctx->Stencil.WriteMask[0]);
   }
   else {
      assert(!ctx->Stencil.Enabled);
   }

   /* vertex positions */
   verts[0].x = x0;
   verts[0].y = y0;
   verts[0].z = z;
   verts[1].x = x1;
   verts[1].y = y0;
   verts[1].z = z;
   verts[2].x = x1;
   verts[2].y = y1;
   verts[2].z = z;
   verts[3].x = x0;
   verts[3].y = y1;
   verts[3].z = z;

   if (!glsl) {
      for (i = 0; i < 4; i++) {
         verts[i].r = ctx->Color.ClearColor.f[0];
         verts[i].g = ctx->Color.ClearColor.f[1];
         verts[i].b = ctx->Color.ClearColor.f[2];
         verts[i].a = ctx->Color.ClearColor.f[3];
      }
   }

   /* upload new vertex data */
   _mesa_buffer_data(ctx, clear->buf_obj, GL_NONE, sizeof(verts), verts,
                     GL_DYNAMIC_DRAW, __func__);

   /* draw quad(s) */
   if (fb->MaxNumLayers > 0) {
      _mesa_DrawArraysInstanced(GL_TRIANGLE_FAN, 0, 4, fb->MaxNumLayers);
   } else {
      _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
   }

   _mesa_meta_end(ctx);
}

/**
 * Meta implementation of ctx->Driver.CopyPixels() in terms
 * of texture mapping and polygon rendering and GLSL shaders.
 */
void
_mesa_meta_CopyPixels(struct gl_context *ctx, GLint srcX, GLint srcY,
                      GLsizei width, GLsizei height,
                      GLint dstX, GLint dstY, GLenum type)
{
   struct copypix_state *copypix = &ctx->Meta->CopyPix;
   struct temp_texture *tex = _mesa_meta_get_temp_texture(ctx);
   struct vertex verts[4];

   if (type != GL_COLOR ||
       ctx->_ImageTransferState ||
       ctx->Fog.Enabled ||
       width > tex->MaxSize ||
       height > tex->MaxSize) {
      /* XXX avoid this fallback */
      _swrast_CopyPixels(ctx, srcX, srcY, width, height, dstX, dstY, type);
      return;
   }

   /* Most GL state applies to glCopyPixels, but a there's a few things
    * we need to override:
    */
   _mesa_meta_begin(ctx, (MESA_META_RASTERIZATION |
                          MESA_META_SHADER |
                          MESA_META_TEXTURE |
                          MESA_META_TRANSFORM |
                          MESA_META_CLIP |
                          MESA_META_VERTEX |
                          MESA_META_VIEWPORT));

   _mesa_meta_setup_vertex_objects(ctx, &copypix->VAO, &copypix->buf_obj, false,
                                   3, 2, 0);

   /* Silence valgrind warnings about reading uninitialized stack. */
   memset(verts, 0, sizeof(verts));

   /* Alloc/setup texture */
   _mesa_meta_setup_copypix_texture(ctx, tex, srcX, srcY, width, height,
                                    GL_RGBA, GL_NEAREST);

   /* vertex positions, texcoords (after texture allocation!) */
   {
      const GLfloat dstX0 = (GLfloat) dstX;
      const GLfloat dstY0 = (GLfloat) dstY;
      const GLfloat dstX1 = dstX + width * ctx->Pixel.ZoomX;
      const GLfloat dstY1 = dstY + height * ctx->Pixel.ZoomY;
      const GLfloat z = invert_z(ctx->Current.RasterPos[2]);

      verts[0].x = dstX0;
      verts[0].y = dstY0;
      verts[0].z = z;
      verts[0].tex[0] = 0.0F;
      verts[0].tex[1] = 0.0F;
      verts[1].x = dstX1;
      verts[1].y = dstY0;
      verts[1].z = z;
      verts[1].tex[0] = tex->Sright;
      verts[1].tex[1] = 0.0F;
      verts[2].x = dstX1;
      verts[2].y = dstY1;
      verts[2].z = z;
      verts[2].tex[0] = tex->Sright;
      verts[2].tex[1] = tex->Ttop;
      verts[3].x = dstX0;
      verts[3].y = dstY1;
      verts[3].z = z;
      verts[3].tex[0] = 0.0F;
      verts[3].tex[1] = tex->Ttop;

      /* upload new vertex data */
      _mesa_buffer_sub_data(ctx, copypix->buf_obj, 0, sizeof(verts), verts,
                            __func__);
   }

   _mesa_set_enable(ctx, tex->Target, GL_TRUE);

   /* draw textured quad */
   _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);

   _mesa_set_enable(ctx, tex->Target, GL_FALSE);

   _mesa_meta_end(ctx);
}

static void
meta_drawpix_cleanup(struct gl_context *ctx, struct drawpix_state *drawpix)
{
   if (drawpix->VAO != 0) {
      _mesa_DeleteVertexArrays(1, &drawpix->VAO);
      drawpix->VAO = 0;

      _mesa_reference_buffer_object(ctx, &drawpix->buf_obj, NULL);
   }

   if (drawpix->StencilFP != 0) {
      _mesa_DeleteProgramsARB(1, &drawpix->StencilFP);
      drawpix->StencilFP = 0;
   }

   if (drawpix->DepthFP != 0) {
      _mesa_DeleteProgramsARB(1, &drawpix->DepthFP);
      drawpix->DepthFP = 0;
   }
}

/**
 * When the glDrawPixels() image size is greater than the max rectangle
 * texture size we use this function to break the glDrawPixels() image
 * into tiles which fit into the max texture size.
 */
static void
tiled_draw_pixels(struct gl_context *ctx,
                  GLint tileSize,
                  GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type,
                  const struct gl_pixelstore_attrib *unpack,
                  const GLvoid *pixels)
{
   struct gl_pixelstore_attrib tileUnpack = *unpack;
   GLint i, j;

   if (tileUnpack.RowLength == 0)
      tileUnpack.RowLength = width;

   for (i = 0; i < width; i += tileSize) {
      const GLint tileWidth = MIN2(tileSize, width - i);
      const GLint tileX = (GLint) (x + i * ctx->Pixel.ZoomX);

      tileUnpack.SkipPixels = unpack->SkipPixels + i;

      for (j = 0; j < height; j += tileSize) {
         const GLint tileHeight = MIN2(tileSize, height - j);
         const GLint tileY = (GLint) (y + j * ctx->Pixel.ZoomY);

         tileUnpack.SkipRows = unpack->SkipRows + j;

         _mesa_meta_DrawPixels(ctx, tileX, tileY, tileWidth, tileHeight,
                               format, type, &tileUnpack, pixels);
      }
   }
}


/**
 * One-time init for drawing stencil pixels.
 */
static void
init_draw_stencil_pixels(struct gl_context *ctx)
{
   /* This program is run eight times, once for each stencil bit.
    * The stencil values to draw are found in an 8-bit alpha texture.
    * We read the texture/stencil value and test if bit 'b' is set.
    * If the bit is not set, use KIL to kill the fragment.
    * Finally, we use the stencil test to update the stencil buffer.
    *
    * The basic algorithm for checking if a bit is set is:
    *   if (is_odd(value / (1 << bit)))
    *      result is one (or non-zero).
    *   else
    *      result is zero.
    * The program parameter contains three values:
    *   parm.x = 255 / (1 << bit)
    *   parm.y = 0.5
    *   parm.z = 0.0
    */
   static const char *program =
      "!!ARBfp1.0\n"
      "PARAM parm = program.local[0]; \n"
      "TEMP t; \n"
      "TEX t, fragment.texcoord[0], texture[0], %s; \n"   /* NOTE %s here! */
      "# t = t * 255 / bit \n"
      "MUL t.x, t.a, parm.x; \n"
      "# t = (int) t \n"
      "FRC t.y, t.x; \n"
      "SUB t.x, t.x, t.y; \n"
      "# t = t * 0.5 \n"
      "MUL t.x, t.x, parm.y; \n"
      "# t = fract(t.x) \n"
      "FRC t.x, t.x; # if t.x != 0, then the bit is set \n"
      "# t.x = (t.x == 0 ? 1 : 0) \n"
      "SGE t.x, -t.x, parm.z; \n"
      "KIL -t.x; \n"
      "# for debug only \n"
      "#MOV result.color, t.x; \n"
      "END \n";
   char program2[1000];
   struct drawpix_state *drawpix = &ctx->Meta->DrawPix;
   struct temp_texture *tex = _mesa_meta_get_temp_texture(ctx);
   const char *texTarget;

   assert(drawpix->StencilFP == 0);

   /* replace %s with "RECT" or "2D" */
   assert(strlen(program) + 4 < sizeof(program2));
   if (tex->Target == GL_TEXTURE_RECTANGLE)
      texTarget = "RECT";
   else
      texTarget = "2D";
   _mesa_snprintf(program2, sizeof(program2), program, texTarget);

   _mesa_GenProgramsARB(1, &drawpix->StencilFP);
   _mesa_BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, drawpix->StencilFP);
   _mesa_ProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
                          strlen(program2), (const GLubyte *) program2);
}


/**
 * One-time init for drawing depth pixels.
 */
static void
init_draw_depth_pixels(struct gl_context *ctx)
{
   static const char *program =
      "!!ARBfp1.0\n"
      "PARAM color = program.local[0]; \n"
      "TEX result.depth, fragment.texcoord[0], texture[0], %s; \n"
      "MOV result.color, color; \n"
      "END \n";
   char program2[200];
   struct drawpix_state *drawpix = &ctx->Meta->DrawPix;
   struct temp_texture *tex = _mesa_meta_get_temp_texture(ctx);
   const char *texTarget;

   assert(drawpix->DepthFP == 0);

   /* replace %s with "RECT" or "2D" */
   assert(strlen(program) + 4 < sizeof(program2));
   if (tex->Target == GL_TEXTURE_RECTANGLE)
      texTarget = "RECT";
   else
      texTarget = "2D";
   _mesa_snprintf(program2, sizeof(program2), program, texTarget);

   _mesa_GenProgramsARB(1, &drawpix->DepthFP);
   _mesa_BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, drawpix->DepthFP);
   _mesa_ProgramStringARB(GL_FRAGMENT_PROGRAM_ARB, GL_PROGRAM_FORMAT_ASCII_ARB,
                          strlen(program2), (const GLubyte *) program2);
}


/**
 * Meta implementation of ctx->Driver.DrawPixels() in terms
 * of texture mapping and polygon rendering.
 */
void
_mesa_meta_DrawPixels(struct gl_context *ctx,
                      GLint x, GLint y, GLsizei width, GLsizei height,
                      GLenum format, GLenum type,
                      const struct gl_pixelstore_attrib *unpack,
                      const GLvoid *pixels)
{
   struct drawpix_state *drawpix = &ctx->Meta->DrawPix;
   struct temp_texture *tex = _mesa_meta_get_temp_texture(ctx);
   const struct gl_pixelstore_attrib unpackSave = ctx->Unpack;
   const GLuint origStencilMask = ctx->Stencil.WriteMask[0];
   struct vertex verts[4];
   GLenum texIntFormat;
   GLboolean fallback, newTex;
   GLbitfield metaExtraSave = 0x0;

   /*
    * Determine if we can do the glDrawPixels with texture mapping.
    */
   fallback = GL_FALSE;
   if (ctx->Fog.Enabled) {
      fallback = GL_TRUE;
   }

   if (_mesa_is_color_format(format)) {
      /* use more compact format when possible */
      /* XXX disable special case for GL_LUMINANCE for now to work around
       * apparent i965 driver bug (see bug #23670).
       */
      if (/*format == GL_LUMINANCE ||*/ format == GL_LUMINANCE_ALPHA)
         texIntFormat = format;
      else
         texIntFormat = GL_RGBA;

      /* If we're not supposed to clamp the resulting color, then just
       * promote our texture to fully float.  We could do better by
       * just going for the matching set of channels, in floating
       * point.
       */
      if (ctx->Color.ClampFragmentColor != GL_TRUE &&
          ctx->Extensions.ARB_texture_float)
         texIntFormat = GL_RGBA32F;
   }
   else if (_mesa_is_stencil_format(format)) {
      if (ctx->Extensions.ARB_fragment_program &&
          ctx->Pixel.IndexShift == 0 &&
          ctx->Pixel.IndexOffset == 0 &&
          type == GL_UNSIGNED_BYTE) {
         /* We'll store stencil as alpha.  This only works for GLubyte
          * image data because of how incoming values are mapped to alpha
          * in [0,1].
          */
         texIntFormat = GL_ALPHA;
         metaExtraSave = (MESA_META_COLOR_MASK |
                          MESA_META_DEPTH_TEST |
                          MESA_META_PIXEL_TRANSFER |
                          MESA_META_SHADER |
                          MESA_META_STENCIL_TEST);
      }
      else {
         fallback = GL_TRUE;
      }
   }
   else if (_mesa_is_depth_format(format)) {
      if (ctx->Extensions.ARB_depth_texture &&
          ctx->Extensions.ARB_fragment_program) {
         texIntFormat = GL_DEPTH_COMPONENT;
         metaExtraSave = (MESA_META_SHADER);
      }
      else {
         fallback = GL_TRUE;
      }
   }
   else {
      fallback = GL_TRUE;
   }

   if (fallback) {
      _swrast_DrawPixels(ctx, x, y, width, height,
                         format, type, unpack, pixels);
      return;
   }

   /*
    * Check image size against max texture size, draw as tiles if needed.
    */
   if (width > tex->MaxSize || height > tex->MaxSize) {
      tiled_draw_pixels(ctx, tex->MaxSize, x, y, width, height,
                        format, type, unpack, pixels);
      return;
   }

   /* Most GL state applies to glDrawPixels (like blending, stencil, etc),
    * but a there's a few things we need to override:
    */
   _mesa_meta_begin(ctx, (MESA_META_RASTERIZATION |
                          MESA_META_SHADER |
                          MESA_META_TEXTURE |
                          MESA_META_TRANSFORM |
                          MESA_META_CLIP |
                          MESA_META_VERTEX |
                          MESA_META_VIEWPORT |
                          metaExtraSave));

   newTex = _mesa_meta_alloc_texture(tex, width, height, texIntFormat);

   _mesa_meta_setup_vertex_objects(ctx, &drawpix->VAO, &drawpix->buf_obj, false,
                                   3, 2, 0);

   /* Silence valgrind warnings about reading uninitialized stack. */
   memset(verts, 0, sizeof(verts));

   /* vertex positions, texcoords (after texture allocation!) */
   {
      const GLfloat x0 = (GLfloat) x;
      const GLfloat y0 = (GLfloat) y;
      const GLfloat x1 = x + width * ctx->Pixel.ZoomX;
      const GLfloat y1 = y + height * ctx->Pixel.ZoomY;
      const GLfloat z = invert_z(ctx->Current.RasterPos[2]);

      verts[0].x = x0;
      verts[0].y = y0;
      verts[0].z = z;
      verts[0].tex[0] = 0.0F;
      verts[0].tex[1] = 0.0F;
      verts[1].x = x1;
      verts[1].y = y0;
      verts[1].z = z;
      verts[1].tex[0] = tex->Sright;
      verts[1].tex[1] = 0.0F;
      verts[2].x = x1;
      verts[2].y = y1;
      verts[2].z = z;
      verts[2].tex[0] = tex->Sright;
      verts[2].tex[1] = tex->Ttop;
      verts[3].x = x0;
      verts[3].y = y1;
      verts[3].z = z;
      verts[3].tex[0] = 0.0F;
      verts[3].tex[1] = tex->Ttop;
   }

   /* upload new vertex data */
   _mesa_buffer_data(ctx, drawpix->buf_obj, GL_NONE, sizeof(verts), verts,
                     GL_DYNAMIC_DRAW, __func__);

   /* set given unpack params */
   ctx->Unpack = *unpack;

   _mesa_set_enable(ctx, tex->Target, GL_TRUE);

   if (_mesa_is_stencil_format(format)) {
      /* Drawing stencil */
      GLint bit;

      if (!drawpix->StencilFP)
         init_draw_stencil_pixels(ctx);

      _mesa_meta_setup_drawpix_texture(ctx, tex, newTex, width, height,
                                       GL_ALPHA, type, pixels);

      _mesa_ColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

      _mesa_set_enable(ctx, GL_STENCIL_TEST, GL_TRUE);

      /* set all stencil bits to 0 */
      _mesa_StencilOp(GL_REPLACE, GL_REPLACE, GL_REPLACE);
      _mesa_StencilFunc(GL_ALWAYS, 0, 255);
      _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
  
      /* set stencil bits to 1 where needed */
      _mesa_StencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);

      _mesa_BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, drawpix->StencilFP);
      _mesa_set_enable(ctx, GL_FRAGMENT_PROGRAM_ARB, GL_TRUE);

      for (bit = 0; bit < ctx->DrawBuffer->Visual.stencilBits; bit++) {
         const GLuint mask = 1 << bit;
         if (mask & origStencilMask) {
            _mesa_StencilFunc(GL_ALWAYS, mask, mask);
            _mesa_StencilMask(mask);

            _mesa_ProgramLocalParameter4fARB(GL_FRAGMENT_PROGRAM_ARB, 0,
                                             255.0f / mask, 0.5f, 0.0f, 0.0f);

            _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
         }
      }
   }
   else if (_mesa_is_depth_format(format)) {
      /* Drawing depth */
      if (!drawpix->DepthFP)
         init_draw_depth_pixels(ctx);

      _mesa_BindProgramARB(GL_FRAGMENT_PROGRAM_ARB, drawpix->DepthFP);
      _mesa_set_enable(ctx, GL_FRAGMENT_PROGRAM_ARB, GL_TRUE);

      /* polygon color = current raster color */
      _mesa_ProgramLocalParameter4fvARB(GL_FRAGMENT_PROGRAM_ARB, 0,
                                        ctx->Current.RasterColor);

      _mesa_meta_setup_drawpix_texture(ctx, tex, newTex, width, height,
                                       format, type, pixels);

      _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
   }
   else {
      /* Drawing RGBA */
      _mesa_meta_setup_drawpix_texture(ctx, tex, newTex, width, height,
                                       format, type, pixels);
      _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
   }

   _mesa_set_enable(ctx, tex->Target, GL_FALSE);

   /* restore unpack params */
   ctx->Unpack = unpackSave;

   _mesa_meta_end(ctx);
}

static GLboolean
alpha_test_raster_color(struct gl_context *ctx)
{
   GLfloat alpha = ctx->Current.RasterColor[ACOMP];
   GLfloat ref = ctx->Color.AlphaRef;

   switch (ctx->Color.AlphaFunc) {
      case GL_NEVER:
         return GL_FALSE;
      case GL_LESS:
         return alpha < ref;
      case GL_EQUAL:
         return alpha == ref;
      case GL_LEQUAL:
         return alpha <= ref;
      case GL_GREATER:
         return alpha > ref;
      case GL_NOTEQUAL:
         return alpha != ref;
      case GL_GEQUAL:
         return alpha >= ref;
      case GL_ALWAYS:
         return GL_TRUE;
      default:
         assert(0);
         return GL_FALSE;
   }
}

/**
 * Do glBitmap with a alpha texture quad.  Use the alpha test to cull
 * the 'off' bits.  A bitmap cache as in the gallium/mesa state
 * tracker would improve performance a lot.
 */
void
_mesa_meta_Bitmap(struct gl_context *ctx,
                  GLint x, GLint y, GLsizei width, GLsizei height,
                  const struct gl_pixelstore_attrib *unpack,
                  const GLubyte *bitmap1)
{
   struct bitmap_state *bitmap = &ctx->Meta->Bitmap;
   struct temp_texture *tex = get_bitmap_temp_texture(ctx);
   const GLenum texIntFormat = GL_ALPHA;
   const struct gl_pixelstore_attrib unpackSave = *unpack;
   GLubyte fg, bg;
   struct vertex verts[4];
   GLboolean newTex;
   GLubyte *bitmap8;

   /*
    * Check if swrast fallback is needed.
    */
   if (ctx->_ImageTransferState ||
       ctx->FragmentProgram._Enabled ||
       ctx->Fog.Enabled ||
       ctx->Texture._MaxEnabledTexImageUnit != -1 ||
       width > tex->MaxSize ||
       height > tex->MaxSize) {
      _swrast_Bitmap(ctx, x, y, width, height, unpack, bitmap1);
      return;
   }

   if (ctx->Color.AlphaEnabled && !alpha_test_raster_color(ctx))
      return;

   /* Most GL state applies to glBitmap (like blending, stencil, etc),
    * but a there's a few things we need to override:
    */
   _mesa_meta_begin(ctx, (MESA_META_ALPHA_TEST |
                          MESA_META_PIXEL_STORE |
                          MESA_META_RASTERIZATION |
                          MESA_META_SHADER |
                          MESA_META_TEXTURE |
                          MESA_META_TRANSFORM |
                          MESA_META_CLIP |
                          MESA_META_VERTEX |
                          MESA_META_VIEWPORT));

   _mesa_meta_setup_vertex_objects(ctx, &bitmap->VAO, &bitmap->buf_obj, false,
                                   3, 2, 4);

   newTex = _mesa_meta_alloc_texture(tex, width, height, texIntFormat);

   /* Silence valgrind warnings about reading uninitialized stack. */
   memset(verts, 0, sizeof(verts));

   /* vertex positions, texcoords, colors (after texture allocation!) */
   {
      const GLfloat x0 = (GLfloat) x;
      const GLfloat y0 = (GLfloat) y;
      const GLfloat x1 = (GLfloat) (x + width);
      const GLfloat y1 = (GLfloat) (y + height);
      const GLfloat z = invert_z(ctx->Current.RasterPos[2]);
      GLuint i;

      verts[0].x = x0;
      verts[0].y = y0;
      verts[0].z = z;
      verts[0].tex[0] = 0.0F;
      verts[0].tex[1] = 0.0F;
      verts[1].x = x1;
      verts[1].y = y0;
      verts[1].z = z;
      verts[1].tex[0] = tex->Sright;
      verts[1].tex[1] = 0.0F;
      verts[2].x = x1;
      verts[2].y = y1;
      verts[2].z = z;
      verts[2].tex[0] = tex->Sright;
      verts[2].tex[1] = tex->Ttop;
      verts[3].x = x0;
      verts[3].y = y1;
      verts[3].z = z;
      verts[3].tex[0] = 0.0F;
      verts[3].tex[1] = tex->Ttop;

      for (i = 0; i < 4; i++) {
         verts[i].r = ctx->Current.RasterColor[0];
         verts[i].g = ctx->Current.RasterColor[1];
         verts[i].b = ctx->Current.RasterColor[2];
         verts[i].a = ctx->Current.RasterColor[3];
      }

      /* upload new vertex data */
      _mesa_buffer_sub_data(ctx, bitmap->buf_obj, 0, sizeof(verts), verts,
                            __func__);
   }

   /* choose different foreground/background alpha values */
   CLAMPED_FLOAT_TO_UBYTE(fg, ctx->Current.RasterColor[ACOMP]);
   bg = (fg > 127 ? 0 : 255);

   bitmap1 = _mesa_map_pbo_source(ctx, &unpackSave, bitmap1);
   if (!bitmap1) {
      _mesa_meta_end(ctx);
      return;
   }

   bitmap8 = malloc(width * height);
   if (bitmap8) {
      memset(bitmap8, bg, width * height);
      _mesa_expand_bitmap(width, height, &unpackSave, bitmap1,
                          bitmap8, width, fg);

      _mesa_set_enable(ctx, tex->Target, GL_TRUE);

      _mesa_set_enable(ctx, GL_ALPHA_TEST, GL_TRUE);
      _mesa_AlphaFunc(GL_NOTEQUAL, UBYTE_TO_FLOAT(bg));

      _mesa_meta_setup_drawpix_texture(ctx, tex, newTex, width, height,
                                       GL_ALPHA, GL_UNSIGNED_BYTE, bitmap8);

      _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);

      _mesa_set_enable(ctx, tex->Target, GL_FALSE);

      free(bitmap8);
   }

   _mesa_unmap_pbo_source(ctx, &unpackSave);

   _mesa_meta_end(ctx);
}

/**
 * Compute the texture coordinates for the four vertices of a quad for
 * drawing a 2D texture image or slice of a cube/3D texture.  The offset
 * and width, height specify a sub-region of the 2D image.
 *
 * \param faceTarget  GL_TEXTURE_1D/2D/3D or cube face name
 * \param slice  slice of a 1D/2D array texture or 3D texture
 * \param xoffset  X position of sub texture
 * \param yoffset  Y position of sub texture
 * \param width  width of the sub texture image
 * \param height  height of the sub texture image
 * \param total_width  total width of the texture image
 * \param total_height  total height of the texture image
 * \param total_depth  total depth of the texture image
 * \param coords0/1/2/3  returns the computed texcoords
 */
void
_mesa_meta_setup_texture_coords(GLenum faceTarget,
                                GLint slice,
                                GLint xoffset,
                                GLint yoffset,
                                GLint width,
                                GLint height,
                                GLint total_width,
                                GLint total_height,
                                GLint total_depth,
                                GLfloat coords0[4],
                                GLfloat coords1[4],
                                GLfloat coords2[4],
                                GLfloat coords3[4])
{
   float st[4][2];
   GLuint i;
   const float s0 = (float) xoffset / (float) total_width;
   const float s1 = (float) (xoffset + width) / (float) total_width;
   const float t0 = (float) yoffset / (float) total_height;
   const float t1 = (float) (yoffset + height) / (float) total_height;
   GLfloat r;

   /* setup the reference texcoords */
   st[0][0] = s0;
   st[0][1] = t0;
   st[1][0] = s1;
   st[1][1] = t0;
   st[2][0] = s1;
   st[2][1] = t1;
   st[3][0] = s0;
   st[3][1] = t1;

   if (faceTarget == GL_TEXTURE_CUBE_MAP_ARRAY)
      faceTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + slice % 6;

   /* Currently all texture targets want the W component to be 1.0.
    */
   coords0[3] = 1.0F;
   coords1[3] = 1.0F;
   coords2[3] = 1.0F;
   coords3[3] = 1.0F;

   switch (faceTarget) {
   case GL_TEXTURE_1D:
   case GL_TEXTURE_2D:
   case GL_TEXTURE_3D:
   case GL_TEXTURE_2D_ARRAY:
      if (faceTarget == GL_TEXTURE_3D) {
         assert(slice < total_depth);
         assert(total_depth >= 1);
         r = (slice + 0.5f) / total_depth;
      }
      else if (faceTarget == GL_TEXTURE_2D_ARRAY)
         r = (float) slice;
      else
         r = 0.0F;
      coords0[0] = st[0][0]; /* s */
      coords0[1] = st[0][1]; /* t */
      coords0[2] = r; /* r */
      coords1[0] = st[1][0];
      coords1[1] = st[1][1];
      coords1[2] = r;
      coords2[0] = st[2][0];
      coords2[1] = st[2][1];
      coords2[2] = r;
      coords3[0] = st[3][0];
      coords3[1] = st[3][1];
      coords3[2] = r;
      break;
   case GL_TEXTURE_RECTANGLE_ARB:
      coords0[0] = (float) xoffset; /* s */
      coords0[1] = (float) yoffset; /* t */
      coords0[2] = 0.0F; /* r */
      coords1[0] = (float) (xoffset + width);
      coords1[1] = (float) yoffset;
      coords1[2] = 0.0F;
      coords2[0] = (float) (xoffset + width);
      coords2[1] = (float) (yoffset + height);
      coords2[2] = 0.0F;
      coords3[0] = (float) xoffset;
      coords3[1] = (float) (yoffset + height);
      coords3[2] = 0.0F;
      break;
   case GL_TEXTURE_1D_ARRAY:
      coords0[0] = st[0][0]; /* s */
      coords0[1] = (float) slice; /* t */
      coords0[2] = 0.0F; /* r */
      coords1[0] = st[1][0];
      coords1[1] = (float) slice;
      coords1[2] = 0.0F;
      coords2[0] = st[2][0];
      coords2[1] = (float) slice;
      coords2[2] = 0.0F;
      coords3[0] = st[3][0];
      coords3[1] = (float) slice;
      coords3[2] = 0.0F;
      break;

   case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      /* loop over quad verts */
      for (i = 0; i < 4; i++) {
         /* Compute sc = +/-scale and tc = +/-scale.
          * Not +/-1 to avoid cube face selection ambiguity near the edges,
          * though that can still sometimes happen with this scale factor...
          */
         const GLfloat scale = 0.9999f;
         const GLfloat sc = (2.0f * st[i][0] - 1.0f) * scale;
         const GLfloat tc = (2.0f * st[i][1] - 1.0f) * scale;
         GLfloat *coord;

         switch (i) {
         case 0:
            coord = coords0;
            break;
         case 1:
            coord = coords1;
            break;
         case 2:
            coord = coords2;
            break;
         case 3:
            coord = coords3;
            break;
         default:
            unreachable("not reached");
         }

         coord[3] = (float) (slice / 6);

         switch (faceTarget) {
         case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
            coord[0] = 1.0f;
            coord[1] = -tc;
            coord[2] = -sc;
            break;
         case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
            coord[0] = -1.0f;
            coord[1] = -tc;
            coord[2] = sc;
            break;
         case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
            coord[0] = sc;
            coord[1] = 1.0f;
            coord[2] = tc;
            break;
         case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
            coord[0] = sc;
            coord[1] = -1.0f;
            coord[2] = -tc;
            break;
         case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
            coord[0] = sc;
            coord[1] = -tc;
            coord[2] = 1.0f;
            break;
         case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
            coord[0] = -sc;
            coord[1] = -tc;
            coord[2] = -1.0f;
            break;
         default:
            assert(0);
         }
      }
      break;
   default:
      assert(!"unexpected target in _mesa_meta_setup_texture_coords()");
   }
}

static struct blit_shader *
choose_blit_shader(GLenum target, struct blit_shader_table *table)
{
   switch(target) {
   case GL_TEXTURE_1D:
      table->sampler_1d.type = "sampler1D";
      table->sampler_1d.func = "texture1D";
      table->sampler_1d.texcoords = "texCoords.x";
      return &table->sampler_1d;
   case GL_TEXTURE_2D:
      table->sampler_2d.type = "sampler2D";
      table->sampler_2d.func = "texture2D";
      table->sampler_2d.texcoords = "texCoords.xy";
      return &table->sampler_2d;
   case GL_TEXTURE_RECTANGLE:
      table->sampler_rect.type = "sampler2DRect";
      table->sampler_rect.func = "texture2DRect";
      table->sampler_rect.texcoords = "texCoords.xy";
      return &table->sampler_rect;
   case GL_TEXTURE_3D:
      /* Code for mipmap generation with 3D textures is not used yet.
       * It's a sw fallback.
       */
      table->sampler_3d.type = "sampler3D";
      table->sampler_3d.func = "texture3D";
      table->sampler_3d.texcoords = "texCoords.xyz";
      return &table->sampler_3d;
   case GL_TEXTURE_CUBE_MAP:
      table->sampler_cubemap.type = "samplerCube";
      table->sampler_cubemap.func = "textureCube";
      table->sampler_cubemap.texcoords = "texCoords.xyz";
      return &table->sampler_cubemap;
   case GL_TEXTURE_1D_ARRAY:
      table->sampler_1d_array.type = "sampler1DArray";
      table->sampler_1d_array.func = "texture1DArray";
      table->sampler_1d_array.texcoords = "texCoords.xy";
      return &table->sampler_1d_array;
   case GL_TEXTURE_2D_ARRAY:
      table->sampler_2d_array.type = "sampler2DArray";
      table->sampler_2d_array.func = "texture2DArray";
      table->sampler_2d_array.texcoords = "texCoords.xyz";
      return &table->sampler_2d_array;
   case GL_TEXTURE_CUBE_MAP_ARRAY:
      table->sampler_cubemap_array.type = "samplerCubeArray";
      table->sampler_cubemap_array.func = "textureCubeArray";
      table->sampler_cubemap_array.texcoords = "texCoords.xyzw";
      return &table->sampler_cubemap_array;
   default:
      _mesa_problem(NULL, "Unexpected texture target 0x%x in"
                    " setup_texture_sampler()\n", target);
      return NULL;
   }
}

void
_mesa_meta_blit_shader_table_cleanup(struct gl_context *ctx,
                                     struct blit_shader_table *table)
{
   _mesa_reference_shader_program(ctx, &table->sampler_1d.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_2d.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_3d.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_rect.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_cubemap.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_1d_array.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_2d_array.shader_prog, NULL);
   _mesa_reference_shader_program(ctx, &table->sampler_cubemap_array.shader_prog, NULL);
}

/**
 * Determine the GL data type to use for the temporary image read with
 * ReadPixels() and passed to Tex[Sub]Image().
 */
static GLenum
get_temp_image_type(struct gl_context *ctx, mesa_format format)
{
   const GLenum baseFormat = _mesa_get_format_base_format(format);
   const GLenum datatype = _mesa_get_format_datatype(format);
   const GLint format_red_bits = _mesa_get_format_bits(format, GL_RED_BITS);

   switch (baseFormat) {
   case GL_RGBA:
   case GL_RGB:
   case GL_RG:
   case GL_RED:
   case GL_ALPHA:
   case GL_LUMINANCE:
   case GL_LUMINANCE_ALPHA:
   case GL_INTENSITY:
      if (datatype == GL_INT || datatype == GL_UNSIGNED_INT) {
         return datatype;
      } else if (format_red_bits <= 8) {
         return GL_UNSIGNED_BYTE;
      } else if (format_red_bits <= 16) {
         return GL_UNSIGNED_SHORT;
      }
      return GL_FLOAT;
   case GL_DEPTH_COMPONENT:
      if (datatype == GL_FLOAT)
         return GL_FLOAT;
      else
         return GL_UNSIGNED_INT;
   case GL_DEPTH_STENCIL:
      if (datatype == GL_FLOAT)
         return GL_FLOAT_32_UNSIGNED_INT_24_8_REV;
      else
         return GL_UNSIGNED_INT_24_8;
   default:
      _mesa_problem(ctx, "Unexpected format %d in get_temp_image_type()",
                    baseFormat);
      return 0;
   }
}

/**
 * Attempts to wrap the destination texture in an FBO and use
 * glBlitFramebuffer() to implement glCopyTexSubImage().
 */
static bool
copytexsubimage_using_blit_framebuffer(struct gl_context *ctx, GLuint dims,
                                       struct gl_texture_image *texImage,
                                       GLint xoffset,
                                       GLint yoffset,
                                       GLint zoffset,
                                       struct gl_renderbuffer *rb,
                                       GLint x, GLint y,
                                       GLsizei width, GLsizei height)
{
   struct gl_framebuffer *drawFb;
   bool success = false;
   GLbitfield mask;
   GLenum status;

   if (!ctx->Extensions.ARB_framebuffer_object)
      return false;

   drawFb = ctx->Driver.NewFramebuffer(ctx, 0xDEADBEEF);
   if (drawFb == NULL)
      return false;

   _mesa_meta_begin(ctx, MESA_META_ALL & ~MESA_META_DRAW_BUFFERS);
   _mesa_bind_framebuffers(ctx, drawFb, ctx->ReadBuffer);

   if (rb->_BaseFormat == GL_DEPTH_STENCIL ||
       rb->_BaseFormat == GL_DEPTH_COMPONENT) {
      _mesa_meta_framebuffer_texture_image(ctx, ctx->DrawBuffer,
                                           GL_DEPTH_ATTACHMENT,
                                           texImage, zoffset);
      mask = GL_DEPTH_BUFFER_BIT;

      if (rb->_BaseFormat == GL_DEPTH_STENCIL &&
          texImage->_BaseFormat == GL_DEPTH_STENCIL) {
         _mesa_meta_framebuffer_texture_image(ctx, ctx->DrawBuffer,
                                              GL_STENCIL_ATTACHMENT,
                                              texImage, zoffset);
         mask |= GL_STENCIL_BUFFER_BIT;
      }
      _mesa_DrawBuffer(GL_NONE);
   } else {
      _mesa_meta_framebuffer_texture_image(ctx, ctx->DrawBuffer,
                                           GL_COLOR_ATTACHMENT0,
                                           texImage, zoffset);
      mask = GL_COLOR_BUFFER_BIT;
      _mesa_DrawBuffer(GL_COLOR_ATTACHMENT0);
   }

   status = _mesa_check_framebuffer_status(ctx, ctx->DrawBuffer);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      goto out;

   ctx->Meta->Blit.no_ctsi_fallback = true;

   /* Since we've bound a new draw framebuffer, we need to update
    * its derived state -- _Xmin, etc -- for BlitFramebuffer's clipping to
    * be correct.
    */
   _mesa_update_state(ctx);

   /* We skip the core BlitFramebuffer checks for format consistency, which
    * are too strict for CopyTexImage.  We know meta will be fine with format
    * changes.
    */
   mask = _mesa_meta_BlitFramebuffer(ctx, ctx->ReadBuffer, ctx->DrawBuffer,
                                     x, y,
                                     x + width, y + height,
                                     xoffset, yoffset,
                                     xoffset + width, yoffset + height,
                                     mask, GL_NEAREST);
   ctx->Meta->Blit.no_ctsi_fallback = false;
   success = mask == 0x0;

 out:
   _mesa_reference_framebuffer(&drawFb, NULL);
   _mesa_meta_end(ctx);
   return success;
}

/**
 * Helper for _mesa_meta_CopyTexSubImage1/2/3D() functions.
 * Have to be careful with locking and meta state for pixel transfer.
 */
void
_mesa_meta_CopyTexSubImage(struct gl_context *ctx, GLuint dims,
                           struct gl_texture_image *texImage,
                           GLint xoffset, GLint yoffset, GLint zoffset,
                           struct gl_renderbuffer *rb,
                           GLint x, GLint y,
                           GLsizei width, GLsizei height)
{
   GLenum format, type;
   GLint bpp;
   void *buf;

   if (copytexsubimage_using_blit_framebuffer(ctx, dims,
                                              texImage,
                                              xoffset, yoffset, zoffset,
                                              rb,
                                              x, y,
                                              width, height)) {
      return;
   }

   /* Choose format/type for temporary image buffer */
   format = _mesa_get_format_base_format(texImage->TexFormat);
   if (format == GL_LUMINANCE ||
       format == GL_LUMINANCE_ALPHA ||
       format == GL_INTENSITY) {
      /* We don't want to use GL_LUMINANCE, GL_INTENSITY, etc. for the
       * temp image buffer because glReadPixels will do L=R+G+B which is
       * not what we want (should be L=R).
       */
      format = GL_RGBA;
   }

   type = get_temp_image_type(ctx, texImage->TexFormat);
   if (_mesa_is_format_integer_color(texImage->TexFormat)) {
      format = _mesa_base_format_to_integer_format(format);
   }
   bpp = _mesa_bytes_per_pixel(format, type);
   if (bpp <= 0) {
      _mesa_problem(ctx, "Bad bpp in _mesa_meta_CopyTexSubImage()");
      return;
   }

   /*
    * Alloc image buffer (XXX could use a PBO)
    */
   buf = malloc(width * height * bpp);
   if (!buf) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glCopyTexSubImage%uD", dims);
      return;
   }

   /*
    * Read image from framebuffer (disable pixel transfer ops)
    */
   _mesa_meta_begin(ctx, MESA_META_PIXEL_STORE | MESA_META_PIXEL_TRANSFER);
   ctx->Driver.ReadPixels(ctx, x, y, width, height,
                          format, type, &ctx->Pack, buf);
   _mesa_meta_end(ctx);

   _mesa_update_state(ctx); /* to update pixel transfer state */

   /*
    * Store texture data (with pixel transfer ops)
    */
   _mesa_meta_begin(ctx, MESA_META_PIXEL_STORE);

   if (texImage->TexObject->Target == GL_TEXTURE_1D_ARRAY) {
      assert(yoffset == 0);
      ctx->Driver.TexSubImage(ctx, dims, texImage,
                              xoffset, zoffset, 0, width, 1, 1,
                              format, type, buf, &ctx->Unpack);
   } else {
      ctx->Driver.TexSubImage(ctx, dims, texImage,
                              xoffset, yoffset, zoffset, width, height, 1,
                              format, type, buf, &ctx->Unpack);
   }

   _mesa_meta_end(ctx);

   free(buf);
}

static void
meta_decompress_fbo_cleanup(struct decompress_fbo_state *decompress_fbo)
{
   if (decompress_fbo->fb != NULL) {
      _mesa_reference_framebuffer(&decompress_fbo->fb, NULL);
      _mesa_reference_renderbuffer(&decompress_fbo->rb, NULL);
   }

   memset(decompress_fbo, 0, sizeof(*decompress_fbo));
}

static void
meta_decompress_cleanup(struct gl_context *ctx,
                        struct decompress_state *decompress)
{
   meta_decompress_fbo_cleanup(&decompress->byteFBO);
   meta_decompress_fbo_cleanup(&decompress->floatFBO);

   if (decompress->VAO != 0) {
      _mesa_DeleteVertexArrays(1, &decompress->VAO);
      _mesa_reference_buffer_object(ctx, &decompress->buf_obj, NULL);
   }

   _mesa_reference_sampler_object(ctx, &decompress->samp_obj, NULL);

   memset(decompress, 0, sizeof(*decompress));
}

/**
 * Decompress a texture image by drawing a quad with the compressed
 * texture and reading the pixels out of the color buffer.
 * \param slice  which slice of a 3D texture or layer of a 1D/2D texture
 * \param destFormat  format, ala glReadPixels
 * \param destType  type, ala glReadPixels
 * \param dest  destination buffer
 * \param destRowLength  dest image rowLength (ala GL_PACK_ROW_LENGTH)
 */
static bool
decompress_texture_image(struct gl_context *ctx,
                         struct gl_texture_image *texImage,
                         GLuint slice,
                         GLint xoffset, GLint yoffset,
                         GLsizei width, GLsizei height,
                         GLenum destFormat, GLenum destType,
                         GLvoid *dest)
{
   struct decompress_state *decompress = &ctx->Meta->Decompress;
   struct decompress_fbo_state *decompress_fbo;
   struct gl_texture_object *texObj = texImage->TexObject;
   const GLenum target = texObj->Target;
   GLenum rbFormat;
   GLenum faceTarget;
   struct vertex verts[4];
   struct gl_sampler_object *samp_obj_save = NULL;
   GLenum status;
   const bool use_glsl_version = ctx->Extensions.ARB_vertex_shader &&
                                      ctx->Extensions.ARB_fragment_shader;

   switch (_mesa_get_format_datatype(texImage->TexFormat)) {
   case GL_FLOAT:
      decompress_fbo = &decompress->floatFBO;
      rbFormat = GL_RGBA32F;
      break;
   case GL_UNSIGNED_NORMALIZED:
      decompress_fbo = &decompress->byteFBO;
      rbFormat = GL_RGBA;
      break;
   default:
      return false;
   }

   if (slice > 0) {
      assert(target == GL_TEXTURE_3D ||
             target == GL_TEXTURE_2D_ARRAY ||
             target == GL_TEXTURE_CUBE_MAP_ARRAY);
   }

   switch (target) {
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY:
      assert(!"No compressed 1D textures.");
      return false;

   case GL_TEXTURE_CUBE_MAP_ARRAY:
      faceTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + (slice % 6);
      break;

   case GL_TEXTURE_CUBE_MAP:
      faceTarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + texImage->Face;
      break;

   default:
      faceTarget = target;
      break;
   }

   _mesa_meta_begin(ctx, MESA_META_ALL & ~(MESA_META_PIXEL_STORE |
                                           MESA_META_DRAW_BUFFERS));
   _mesa_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

   _mesa_reference_sampler_object(ctx, &samp_obj_save,
                                  ctx->Texture.Unit[ctx->Texture.CurrentUnit].Sampler);

   /* Create/bind FBO/renderbuffer */
   if (decompress_fbo->fb == NULL) {
      decompress_fbo->rb = ctx->Driver.NewRenderbuffer(ctx, 0xDEADBEEF);
      if (decompress_fbo->rb == NULL) {
         _mesa_meta_end(ctx);
         return false;
      }

      decompress_fbo->rb->RefCount = 1;

      decompress_fbo->fb = ctx->Driver.NewFramebuffer(ctx, 0xDEADBEEF);
      if (decompress_fbo->fb == NULL) {
         _mesa_meta_end(ctx);
         return false;
      }

      _mesa_bind_framebuffers(ctx, decompress_fbo->fb, decompress_fbo->fb);
      _mesa_framebuffer_renderbuffer(ctx, ctx->DrawBuffer, GL_COLOR_ATTACHMENT0,
                                     decompress_fbo->rb);
   }
   else {
      _mesa_bind_framebuffers(ctx, decompress_fbo->fb, decompress_fbo->fb);
   }

   /* alloc dest surface */
   if (width > decompress_fbo->Width || height > decompress_fbo->Height) {
      _mesa_renderbuffer_storage(ctx, decompress_fbo->rb, rbFormat,
                                 width, height, 0);
      status = _mesa_check_framebuffer_status(ctx, ctx->DrawBuffer);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
         /* If the framebuffer isn't complete then we'll leave
          * decompress_fbo->Width as zero so that it will fail again next time
          * too */
         _mesa_meta_end(ctx);
         return false;
      }
      decompress_fbo->Width = width;
      decompress_fbo->Height = height;
   }

   if (use_glsl_version) {
      _mesa_meta_setup_vertex_objects(ctx, &decompress->VAO,
                                      &decompress->buf_obj, true,
                                      2, 4, 0);

      _mesa_meta_setup_blit_shader(ctx, target, false, &decompress->shaders);
   } else {
      _mesa_meta_setup_ff_tnl_for_blit(ctx, &decompress->VAO,
                                       &decompress->buf_obj, 3);
   }

   if (decompress->samp_obj == NULL) {
      decompress->samp_obj =  ctx->Driver.NewSamplerObject(ctx, 0xDEADBEEF);
      if (decompress->samp_obj == NULL) {
         _mesa_meta_end(ctx);

         /* This is a bit lazy.  Flag out of memory, and then don't bother to
          * clean up.  Once out of memory is flagged, the only realistic next
          * move is to destroy the context.  That will trigger all the right
          * clean up.
          *
          * Returning true prevents other GetTexImage methods from attempting
          * anything since they will likely fail too.
          */
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glGetTexImage");
         return true;
      }

      /* nearest filtering */
      _mesa_set_sampler_filters(ctx, decompress->samp_obj, GL_NEAREST, GL_NEAREST);

      /* We don't want to encode or decode sRGB values; treat them as linear. */
      _mesa_set_sampler_srgb_decode(ctx, decompress->samp_obj, GL_SKIP_DECODE_EXT);
   }

   _mesa_bind_sampler(ctx, ctx->Texture.CurrentUnit, decompress->samp_obj);

   /* Silence valgrind warnings about reading uninitialized stack. */
   memset(verts, 0, sizeof(verts));

   _mesa_meta_setup_texture_coords(faceTarget, slice,
                                   xoffset, yoffset, width, height,
                                   texImage->Width, texImage->Height,
                                   texImage->Depth,
                                   verts[0].tex,
                                   verts[1].tex,
                                   verts[2].tex,
                                   verts[3].tex);

   /* setup vertex positions */
   verts[0].x = -1.0F;
   verts[0].y = -1.0F;
   verts[1].x =  1.0F;
   verts[1].y = -1.0F;
   verts[2].x =  1.0F;
   verts[2].y =  1.0F;
   verts[3].x = -1.0F;
   verts[3].y =  1.0F;

   _mesa_set_viewport(ctx, 0, 0, 0, width, height);

   /* upload new vertex data */
   _mesa_buffer_sub_data(ctx, decompress->buf_obj, 0, sizeof(verts), verts,
                         __func__);

   /* setup texture state */
   _mesa_BindTexture(target, texObj->Name);

   if (!use_glsl_version)
      _mesa_set_enable(ctx, target, GL_TRUE);

   {
      /* save texture object state */
      const GLint baseLevelSave = texObj->BaseLevel;
      const GLint maxLevelSave = texObj->MaxLevel;

      /* restrict sampling to the texture level of interest */
      if (target != GL_TEXTURE_RECTANGLE_ARB) {
         _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_BASE_LEVEL,
                                   (GLint *) &texImage->Level, false);
         _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_MAX_LEVEL,
                                   (GLint *) &texImage->Level, false);
      }

      /* render quad w/ texture into renderbuffer */
      _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
      
      /* Restore texture object state, the texture binding will
       * be restored by _mesa_meta_end().
       */
      if (target != GL_TEXTURE_RECTANGLE_ARB) {
         _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_BASE_LEVEL,
                                   &baseLevelSave, false);
         _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_MAX_LEVEL,
                                   &maxLevelSave, false);
      }

   }

   /* read pixels from renderbuffer */
   {
      GLenum baseTexFormat = texImage->_BaseFormat;
      GLenum destBaseFormat = _mesa_unpack_format_to_base_format(destFormat);

      /* The pixel transfer state will be set to default values at this point
       * (see MESA_META_PIXEL_TRANSFER) so pixel transfer ops are effectively
       * turned off (as required by glGetTexImage) but we need to handle some
       * special cases.  In particular, single-channel texture values are
       * returned as red and two-channel texture values are returned as
       * red/alpha.
       */
      if (_mesa_need_luminance_to_rgb_conversion(baseTexFormat,
                                                 destBaseFormat) ||
          /* If we're reading back an RGB(A) texture (using glGetTexImage) as
           * luminance then we need to return L=tex(R).
           */
          _mesa_need_rgb_to_luminance_conversion(baseTexFormat,
                                                 destBaseFormat)) {
         /* Green and blue must be zero */
         _mesa_PixelTransferf(GL_GREEN_SCALE, 0.0f);
         _mesa_PixelTransferf(GL_BLUE_SCALE, 0.0f);
      }

      _mesa_ReadPixels(0, 0, width, height, destFormat, destType, dest);
   }

   /* disable texture unit */
   if (!use_glsl_version)
      _mesa_set_enable(ctx, target, GL_FALSE);

   _mesa_bind_sampler(ctx, ctx->Texture.CurrentUnit, samp_obj_save);
   _mesa_reference_sampler_object(ctx, &samp_obj_save, NULL);

   _mesa_meta_end(ctx);

   return true;
}


/**
 * This is just a wrapper around _mesa_get_tex_image() and
 * decompress_texture_image().  Meta functions should not be directly called
 * from core Mesa.
 */
void
_mesa_meta_GetTexSubImage(struct gl_context *ctx,
                          GLint xoffset, GLint yoffset, GLint zoffset,
                          GLsizei width, GLsizei height, GLsizei depth,
                          GLenum format, GLenum type, GLvoid *pixels,
                          struct gl_texture_image *texImage)
{
   if (_mesa_is_format_compressed(texImage->TexFormat)) {
      GLuint slice;
      bool result = true;

      for (slice = 0; slice < depth; slice++) {
         void *dst;
         /* Section 8.11.4 (Texture Image Queries) of the GL 4.5 spec says:
          *
          *    "For three-dimensional, two-dimensional array, cube map array,
          *     and cube map textures pixel storage operations are applied as
          *     if the image were two-dimensional, except that the additional
          *     pixel storage state values PACK_IMAGE_HEIGHT and
          *     PACK_SKIP_IMAGES are applied. The correspondence of texels to
          *     memory locations is as defined for TexImage3D in section 8.5."
          */
         switch (texImage->TexObject->Target) {
         case GL_TEXTURE_3D:
         case GL_TEXTURE_2D_ARRAY:
         case GL_TEXTURE_CUBE_MAP:
         case GL_TEXTURE_CUBE_MAP_ARRAY: {
            /* Setup pixel packing.  SkipPixels and SkipRows will be applied
             * in the decompress_texture_image() function's call to
             * glReadPixels but we need to compute the dest slice's address
             * here (according to SkipImages and ImageHeight).
             */
            struct gl_pixelstore_attrib packing = ctx->Pack;
            packing.SkipPixels = 0;
            packing.SkipRows = 0;
            dst = _mesa_image_address3d(&packing, pixels, width, height,
                                        format, type, slice, 0, 0);
            break;
         }
         default:
            dst = pixels;
            break;
         }
         result = decompress_texture_image(ctx, texImage, slice,
                                           xoffset, yoffset, width, height,
                                           format, type, dst);
         if (!result)
            break;
      }

      if (result)
         return;
   }

   _mesa_GetTexSubImage_sw(ctx, xoffset, yoffset, zoffset,
                           width, height, depth, format, type, pixels, texImage);
}


/**
 * Meta implementation of ctx->Driver.DrawTex() in terms
 * of polygon rendering.
 */
void
_mesa_meta_DrawTex(struct gl_context *ctx, GLfloat x, GLfloat y, GLfloat z,
                   GLfloat width, GLfloat height)
{
   struct drawtex_state *drawtex = &ctx->Meta->DrawTex;
   struct vertex {
      GLfloat x, y, z, st[MAX_TEXTURE_UNITS][2];
   };
   struct vertex verts[4];
   GLuint i;

   _mesa_meta_begin(ctx, (MESA_META_RASTERIZATION |
                          MESA_META_SHADER |
                          MESA_META_TRANSFORM |
                          MESA_META_VERTEX |
                          MESA_META_VIEWPORT));

   if (drawtex->VAO == 0) {
      /* one-time setup */
      struct gl_vertex_array_object *array_obj;

      /* create vertex array object */
      _mesa_GenVertexArrays(1, &drawtex->VAO);
      _mesa_BindVertexArray(drawtex->VAO);

      array_obj = _mesa_lookup_vao(ctx, drawtex->VAO);
      assert(array_obj != NULL);

      /* create vertex array buffer */
      drawtex->buf_obj = ctx->Driver.NewBufferObject(ctx, 0xDEADBEEF);
      if (drawtex->buf_obj == NULL)
         return;

      _mesa_buffer_data(ctx, drawtex->buf_obj, GL_NONE, sizeof(verts), verts,
                        GL_DYNAMIC_DRAW, __func__);

      /* setup vertex arrays */
      _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_POS,
                                3, GL_FLOAT, GL_RGBA, GL_FALSE,
                                GL_FALSE, GL_FALSE,
                                offsetof(struct vertex, x), true);
      _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_POS,
                               drawtex->buf_obj, 0, sizeof(struct vertex));
      _mesa_enable_vertex_array_attrib(ctx, array_obj, VERT_ATTRIB_POS);


      for (i = 0; i < ctx->Const.MaxTextureUnits; i++) {
         _mesa_update_array_format(ctx, array_obj, VERT_ATTRIB_TEX(i),
                                   2, GL_FLOAT, GL_RGBA, GL_FALSE,
                                   GL_FALSE, GL_FALSE,
                                   offsetof(struct vertex, st[i]), true);
         _mesa_bind_vertex_buffer(ctx, array_obj, VERT_ATTRIB_TEX(i),
                                  drawtex->buf_obj, 0, sizeof(struct vertex));
         _mesa_enable_vertex_array_attrib(ctx, array_obj, VERT_ATTRIB_TEX(i));
      }
   }
   else {
      _mesa_BindVertexArray(drawtex->VAO);
   }

   /* vertex positions, texcoords */
   {
      const GLfloat x1 = x + width;
      const GLfloat y1 = y + height;

      z = CLAMP(z, 0.0f, 1.0f);
      z = invert_z(z);

      verts[0].x = x;
      verts[0].y = y;
      verts[0].z = z;

      verts[1].x = x1;
      verts[1].y = y;
      verts[1].z = z;

      verts[2].x = x1;
      verts[2].y = y1;
      verts[2].z = z;

      verts[3].x = x;
      verts[3].y = y1;
      verts[3].z = z;

      for (i = 0; i < ctx->Const.MaxTextureUnits; i++) {
         const struct gl_texture_object *texObj;
         const struct gl_texture_image *texImage;
         GLfloat s, t, s1, t1;
         GLuint tw, th;

         if (!ctx->Texture.Unit[i]._Current) {
            GLuint j;
            for (j = 0; j < 4; j++) {
               verts[j].st[i][0] = 0.0f;
               verts[j].st[i][1] = 0.0f;
            }
            continue;
         }

         texObj = ctx->Texture.Unit[i]._Current;
         texImage = texObj->Image[0][texObj->BaseLevel];
         tw = texImage->Width2;
         th = texImage->Height2;

         s = (GLfloat) texObj->CropRect[0] / tw;
         t = (GLfloat) texObj->CropRect[1] / th;
         s1 = (GLfloat) (texObj->CropRect[0] + texObj->CropRect[2]) / tw;
         t1 = (GLfloat) (texObj->CropRect[1] + texObj->CropRect[3]) / th;

         verts[0].st[i][0] = s;
         verts[0].st[i][1] = t;

         verts[1].st[i][0] = s1;
         verts[1].st[i][1] = t;

         verts[2].st[i][0] = s1;
         verts[2].st[i][1] = t1;

         verts[3].st[i][0] = s;
         verts[3].st[i][1] = t1;
      }

      _mesa_buffer_sub_data(ctx, drawtex->buf_obj, 0, sizeof(verts), verts,
                            __func__);
   }

   _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);

   _mesa_meta_end(ctx);
}

static bool
cleartexsubimage_color(struct gl_context *ctx,
                       struct gl_texture_image *texImage,
                       const GLvoid *clearValue,
                       GLint zoffset)
{
   mesa_format format;
   union gl_color_union colorValue;
   GLenum datatype;
   GLenum status;

   _mesa_meta_framebuffer_texture_image(ctx, ctx->DrawBuffer,
                                        GL_COLOR_ATTACHMENT0,
                                        texImage, zoffset);

   status = _mesa_check_framebuffer_status(ctx, ctx->DrawBuffer);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      return false;

   /* We don't want to apply an sRGB conversion so override the format */
   format = _mesa_get_srgb_format_linear(texImage->TexFormat);
   datatype = _mesa_get_format_datatype(format);

   switch (datatype) {
   case GL_UNSIGNED_INT:
   case GL_INT:
      if (clearValue)
         _mesa_unpack_uint_rgba_row(format, 1, clearValue,
                                    (GLuint (*)[4]) colorValue.ui);
      else
         memset(&colorValue, 0, sizeof colorValue);
      if (datatype == GL_INT)
         _mesa_ClearBufferiv(GL_COLOR, 0, colorValue.i);
      else
         _mesa_ClearBufferuiv(GL_COLOR, 0, colorValue.ui);
      break;
   default:
      if (clearValue)
         _mesa_unpack_rgba_row(format, 1, clearValue,
                               (GLfloat (*)[4]) colorValue.f);
      else
         memset(&colorValue, 0, sizeof colorValue);
      _mesa_ClearBufferfv(GL_COLOR, 0, colorValue.f);
      break;
   }

   return true;
}

static bool
cleartexsubimage_depth_stencil(struct gl_context *ctx,
                               struct gl_texture_image *texImage,
                               const GLvoid *clearValue,
                               GLint zoffset)
{
   GLint stencilValue;
   GLfloat depthValue;
   GLenum status;

   _mesa_meta_framebuffer_texture_image(ctx, ctx->DrawBuffer,
                                        GL_DEPTH_ATTACHMENT,
                                        texImage, zoffset);

   if (texImage->_BaseFormat == GL_DEPTH_STENCIL)
      _mesa_meta_framebuffer_texture_image(ctx, ctx->DrawBuffer,
                                           GL_STENCIL_ATTACHMENT,
                                           texImage, zoffset);

   status = _mesa_check_framebuffer_status(ctx, ctx->DrawBuffer);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      return false;

   if (clearValue) {
      GLuint depthStencilValue[2];

      /* Convert the clearValue from whatever format it's in to a floating
       * point value for the depth and an integer value for the stencil index
       */
      _mesa_unpack_float_32_uint_24_8_depth_stencil_row(texImage->TexFormat,
                                                        1, /* n */
                                                        clearValue,
                                                        depthStencilValue);
      /* We need a memcpy here instead of a cast because we need to
       * reinterpret the bytes as a float rather than converting it
       */
      memcpy(&depthValue, depthStencilValue, sizeof depthValue);
      stencilValue = depthStencilValue[1] & 0xff;
   } else {
      depthValue = 0.0f;
      stencilValue = 0;
   }

   if (texImage->_BaseFormat == GL_DEPTH_STENCIL)
      _mesa_ClearBufferfi(GL_DEPTH_STENCIL, 0, depthValue, stencilValue);
   else
      _mesa_ClearBufferfv(GL_DEPTH, 0, &depthValue);

   return true;
}

static bool
cleartexsubimage_for_zoffset(struct gl_context *ctx,
                             struct gl_texture_image *texImage,
                             GLint zoffset,
                             const GLvoid *clearValue)
{
   struct gl_framebuffer *drawFb;
   bool success;

   drawFb = ctx->Driver.NewFramebuffer(ctx, 0xDEADBEEF);
   if (drawFb == NULL)
      return false;

   _mesa_bind_framebuffers(ctx, drawFb, ctx->ReadBuffer);

   switch(texImage->_BaseFormat) {
   case GL_DEPTH_STENCIL:
   case GL_DEPTH_COMPONENT:
      success = cleartexsubimage_depth_stencil(ctx, texImage,
                                               clearValue, zoffset);
      break;
   default:
      success = cleartexsubimage_color(ctx, texImage, clearValue, zoffset);
      break;
   }

   _mesa_reference_framebuffer(&drawFb, NULL);

   return success;
}

static bool
cleartexsubimage_using_fbo(struct gl_context *ctx,
                           struct gl_texture_image *texImage,
                           GLint xoffset, GLint yoffset, GLint zoffset,
                           GLsizei width, GLsizei height, GLsizei depth,
                           const GLvoid *clearValue)
{
   bool success = true;
   GLint z;

   _mesa_meta_begin(ctx,
                    MESA_META_SCISSOR |
                    MESA_META_COLOR_MASK |
                    MESA_META_DITHER |
                    MESA_META_FRAMEBUFFER_SRGB);

   _mesa_ColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
   _mesa_set_enable(ctx, GL_DITHER, GL_FALSE);

   _mesa_set_enable(ctx, GL_SCISSOR_TEST, GL_TRUE);
   _mesa_Scissor(xoffset, yoffset, width, height);

   for (z = zoffset; z < zoffset + depth; z++) {
      if (!cleartexsubimage_for_zoffset(ctx, texImage, z, clearValue)) {
         success = false;
         break;
      }
   }

   _mesa_meta_end(ctx);

   return success;
}

extern void
_mesa_meta_ClearTexSubImage(struct gl_context *ctx,
                            struct gl_texture_image *texImage,
                            GLint xoffset, GLint yoffset, GLint zoffset,
                            GLsizei width, GLsizei height, GLsizei depth,
                            const GLvoid *clearValue)
{
   bool res;

   res = cleartexsubimage_using_fbo(ctx, texImage,
                                    xoffset, yoffset, zoffset,
                                    width, height, depth,
                                    clearValue);

   if (res)
      return;

   _mesa_warning(ctx,
                 "Falling back to mapping the texture in "
                 "glClearTexSubImage\n");

   _mesa_store_cleartexsubimage(ctx, texImage,
                                xoffset, yoffset, zoffset,
                                width, height, depth,
                                clearValue);
}
