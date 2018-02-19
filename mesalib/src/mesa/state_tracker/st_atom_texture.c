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

 /*
  * Authors:
  *   Keith Whitwell <keithw@vmware.com>
  *   Brian Paul
  */


#include "main/context.h"
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/samplerobj.h"
#include "main/teximage.h"
#include "main/texobj.h"
#include "program/prog_instruction.h"

#include "st_context.h"
#include "st_atom.h"
#include "st_sampler_view.h"
#include "st_texture.h"
#include "st_format.h"
#include "st_cb_texture.h"
#include "pipe/p_context.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "cso_cache/cso_context.h"


/**
 * Get a pipe_sampler_view object from a texture unit.
 */
void
st_update_single_texture(struct st_context *st,
                         struct pipe_sampler_view **sampler_view,
                         GLuint texUnit, bool glsl130_or_later,
                         bool ignore_srgb_decode)
{
   struct gl_context *ctx = st->ctx;
   const struct gl_sampler_object *samp;
   struct gl_texture_object *texObj;
   struct st_texture_object *stObj;

   samp = _mesa_get_samplerobj(ctx, texUnit);

   texObj = ctx->Texture.Unit[texUnit]._Current;
   assert(texObj);

   stObj = st_texture_object(texObj);

   if (unlikely(texObj->Target == GL_TEXTURE_BUFFER)) {
      *sampler_view = st_get_buffer_sampler_view_from_stobj(st, stObj);
      return;
   }

   if (!st_finalize_texture(ctx, st->pipe, texObj, 0) ||
       !stObj->pt) {
      /* out of mem */
      *sampler_view = NULL;
      return;
   }

   if (texObj->TargetIndex == TEXTURE_EXTERNAL_INDEX &&
       stObj->pt->screen->resource_changed)
         stObj->pt->screen->resource_changed(stObj->pt->screen, stObj->pt);

   *sampler_view =
      st_get_texture_sampler_view_from_stobj(st, stObj, samp,
                                             glsl130_or_later,
                                             ignore_srgb_decode);
}



static void
update_textures(struct st_context *st,
                enum pipe_shader_type shader_stage,
                const struct gl_program *prog,
                struct pipe_sampler_view **sampler_views)
{
   const GLuint old_max = st->state.num_sampler_views[shader_stage];
   GLbitfield samplers_used = prog->SamplersUsed;
   GLbitfield texel_fetch_samplers = prog->info.textures_used_by_txf;
   GLbitfield free_slots = ~prog->SamplersUsed;
   GLbitfield external_samplers_used = prog->ExternalSamplersUsed;
   GLuint unit;

   if (samplers_used == 0x0 && old_max == 0)
      return;

   unsigned num_textures = 0;

   /* prog->sh.data is NULL if it's ARB_fragment_program */
   bool glsl130 = (prog->sh.data ? prog->sh.data->Version : 0) >= 130;

   /* loop over sampler units (aka tex image units) */
   for (unit = 0; samplers_used || unit < old_max;
        unit++, samplers_used >>= 1, texel_fetch_samplers >>= 1) {
      struct pipe_sampler_view *sampler_view = NULL;

      if (samplers_used & 1) {
         const GLuint texUnit = prog->SamplerUnits[unit];

         /* The EXT_texture_sRGB_decode extension says:
          *
          *    "The conversion of sRGB color space components to linear color
          *     space is always performed if the texel lookup function is one
          *     of the texelFetch builtin functions.
          *
          *     Otherwise, if the texel lookup function is one of the texture
          *     builtin functions or one of the texture gather functions, the
          *     conversion of sRGB color space components to linear color space
          *     is controlled by the TEXTURE_SRGB_DECODE_EXT parameter.
          *
          *     If the TEXTURE_SRGB_DECODE_EXT parameter is DECODE_EXT, the
          *     conversion of sRGB color space components to linear color space
          *     is performed.
          *
          *     If the TEXTURE_SRGB_DECODE_EXT parameter is SKIP_DECODE_EXT,
          *     the value is returned without decoding. However, if the texture
          *     is also [statically] accessed with a texelFetch function, then
          *     the result of texture builtin functions and/or texture gather
          *     functions may be returned with decoding or without decoding."
          *
          * Note: the "statically" will be added to the language per
          *       https://cvs.khronos.org/bugzilla/show_bug.cgi?id=14934
          *
          * So we simply ignore the setting entirely for samplers that are
          * (statically) accessed with a texelFetch function.
          */
         st_update_single_texture(st, &sampler_view, texUnit, glsl130,
                                  texel_fetch_samplers & 1);
         num_textures = unit + 1;
      }

      pipe_sampler_view_reference(&(sampler_views[unit]), sampler_view);
   }

   /* For any external samplers with multiplaner YUV, stuff the additional
    * sampler views we need at the end.
    *
    * Trying to cache the sampler view in the stObj looks painful, so just
    * re-create the sampler view for the extra planes each time.  Main use
    * case is video playback (ie. fps games wouldn't be using this) so I
    * guess no point to try to optimize this feature.
    */
   while (unlikely(external_samplers_used)) {
      GLuint unit = u_bit_scan(&external_samplers_used);
      GLuint extra = 0;
      struct st_texture_object *stObj =
            st_get_texture_object(st->ctx, prog, unit);
      struct pipe_sampler_view tmpl;

      if (!stObj)
         continue;

      /* use original view as template: */
      tmpl = *sampler_views[unit];

      switch (st_get_view_format(stObj)) {
      case PIPE_FORMAT_NV12:
         /* we need one additional R8G8 view: */
         tmpl.format = PIPE_FORMAT_RG88_UNORM;
         tmpl.swizzle_g = PIPE_SWIZZLE_Y;   /* tmpl from Y plane is R8 */
         extra = u_bit_scan(&free_slots);
         sampler_views[extra] =
               st->pipe->create_sampler_view(st->pipe, stObj->pt->next, &tmpl);
         break;
      case PIPE_FORMAT_IYUV:
         /* we need two additional R8 views: */
         tmpl.format = PIPE_FORMAT_R8_UNORM;
         extra = u_bit_scan(&free_slots);
         sampler_views[extra] =
               st->pipe->create_sampler_view(st->pipe, stObj->pt->next, &tmpl);
         extra = u_bit_scan(&free_slots);
         sampler_views[extra] =
               st->pipe->create_sampler_view(st->pipe, stObj->pt->next->next, &tmpl);
         break;
      default:
         break;
      }

      num_textures = MAX2(num_textures, extra + 1);
   }

   cso_set_sampler_views(st->cso_context,
                         shader_stage,
                         num_textures,
                         sampler_views);
   st->state.num_sampler_views[shader_stage] = num_textures;
}

/* Same as update_textures, but don't store the views in st_context. */
static void
update_textures_local(struct st_context *st,
                      enum pipe_shader_type shader_stage,
                      const struct gl_program *prog)
{
   struct pipe_sampler_view *local_views[PIPE_MAX_SAMPLERS] = {0};

   update_textures(st, shader_stage, prog, local_views);

   unsigned num = st->state.num_sampler_views[shader_stage];
   for (unsigned i = 0; i < num; i++)
      pipe_sampler_view_reference(&local_views[i], NULL);
}

void
st_update_vertex_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits > 0) {
      update_textures_local(st, PIPE_SHADER_VERTEX,
                            ctx->VertexProgram._Current);
   }
}


void
st_update_fragment_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   update_textures(st,
                   PIPE_SHADER_FRAGMENT,
                   ctx->FragmentProgram._Current,
                   st->state.frag_sampler_views);
}


void
st_update_geometry_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->GeometryProgram._Current) {
      update_textures_local(st, PIPE_SHADER_GEOMETRY,
                            ctx->GeometryProgram._Current);
   }
}


void
st_update_tessctrl_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->TessCtrlProgram._Current) {
      update_textures_local(st, PIPE_SHADER_TESS_CTRL,
                            ctx->TessCtrlProgram._Current);
   }
}


void
st_update_tesseval_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->TessEvalProgram._Current) {
      update_textures_local(st, PIPE_SHADER_TESS_EVAL,
                            ctx->TessEvalProgram._Current);
   }
}


void
st_update_compute_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->ComputeProgram._Current) {
      update_textures_local(st, PIPE_SHADER_COMPUTE,
                            ctx->ComputeProgram._Current);
   }
}
