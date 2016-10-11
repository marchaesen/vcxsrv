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


static GLboolean
update_single_texture(struct st_context *st,
                      struct pipe_sampler_view **sampler_view,
		      GLuint texUnit, unsigned glsl_version)
{
   struct gl_context *ctx = st->ctx;
   const struct gl_sampler_object *samp;
   struct gl_texture_object *texObj;
   struct st_texture_object *stObj;
   GLboolean retval;

   samp = _mesa_get_samplerobj(ctx, texUnit);

   texObj = ctx->Texture.Unit[texUnit]._Current;

   if (!texObj) {
      texObj = _mesa_get_fallback_texture(ctx, TEXTURE_2D_INDEX);
      samp = &texObj->Sampler;
   }
   stObj = st_texture_object(texObj);

   retval = st_finalize_texture(ctx, st->pipe, texObj);
   if (!retval) {
      /* out of mem */
      return GL_FALSE;
   }

   /* Check a few pieces of state outside the texture object to see if we
    * need to force revalidation.
    */
   if (stObj->prev_glsl_version != glsl_version ||
       stObj->prev_sRGBDecode != samp->sRGBDecode) {

      st_texture_release_all_sampler_views(st, stObj);

      stObj->prev_glsl_version = glsl_version;
      stObj->prev_sRGBDecode = samp->sRGBDecode;
   }

   *sampler_view =
      st_get_texture_sampler_view_from_stobj(st, stObj, samp, glsl_version);
   return GL_TRUE;
}



static void
update_textures(struct st_context *st,
                gl_shader_stage mesa_shader,
                const struct gl_program *prog,
                unsigned max_units,
                struct pipe_sampler_view **sampler_views,
                unsigned *num_textures)
{
   const GLuint old_max = *num_textures;
   GLbitfield samplers_used = prog->SamplersUsed;
   GLbitfield free_slots = ~prog->SamplersUsed;
   GLbitfield external_samplers_used = prog->ExternalSamplersUsed;
   GLuint unit;
   struct gl_shader_program *shader =
      st->ctx->_Shader->CurrentProgram[mesa_shader];
   unsigned glsl_version = shader ? shader->Version : 0;
   enum pipe_shader_type shader_stage = st_shader_stage_to_ptarget(mesa_shader);

   if (samplers_used == 0x0 && old_max == 0)
      return;

   *num_textures = 0;

   /* loop over sampler units (aka tex image units) */
   for (unit = 0; unit < max_units; unit++, samplers_used >>= 1) {
      struct pipe_sampler_view *sampler_view = NULL;

      if (samplers_used & 1) {
         const GLuint texUnit = prog->SamplerUnits[unit];
         GLboolean retval;

         retval = update_single_texture(st, &sampler_view, texUnit,
                                        glsl_version);
         if (retval == GL_FALSE)
            continue;

         *num_textures = unit + 1;
      }
      else if (samplers_used == 0 && unit >= old_max) {
         /* if we've reset all the old views and we have no more new ones */
         break;
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

      *num_textures = MAX2(*num_textures, extra + 1);
   }

   cso_set_sampler_views(st->cso_context,
                         shader_stage,
                         *num_textures,
                         sampler_views);
}



static void
update_vertex_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits > 0) {
      update_textures(st,
                      MESA_SHADER_VERTEX,
                      &ctx->VertexProgram._Current->Base,
                      ctx->Const.Program[MESA_SHADER_VERTEX].MaxTextureImageUnits,
                      st->state.sampler_views[PIPE_SHADER_VERTEX],
                      &st->state.num_sampler_views[PIPE_SHADER_VERTEX]);
   }
}


static void
update_fragment_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   update_textures(st,
                   MESA_SHADER_FRAGMENT,
                   &ctx->FragmentProgram._Current->Base,
                   ctx->Const.Program[MESA_SHADER_FRAGMENT].MaxTextureImageUnits,
                   st->state.sampler_views[PIPE_SHADER_FRAGMENT],
                   &st->state.num_sampler_views[PIPE_SHADER_FRAGMENT]);
}


static void
update_geometry_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->GeometryProgram._Current) {
      update_textures(st,
                      MESA_SHADER_GEOMETRY,
                      &ctx->GeometryProgram._Current->Base,
                      ctx->Const.Program[MESA_SHADER_GEOMETRY].MaxTextureImageUnits,
                      st->state.sampler_views[PIPE_SHADER_GEOMETRY],
                      &st->state.num_sampler_views[PIPE_SHADER_GEOMETRY]);
   }
}


static void
update_tessctrl_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->TessCtrlProgram._Current) {
      update_textures(st,
                      MESA_SHADER_TESS_CTRL,
                      &ctx->TessCtrlProgram._Current->Base,
                      ctx->Const.Program[MESA_SHADER_TESS_CTRL].MaxTextureImageUnits,
                      st->state.sampler_views[PIPE_SHADER_TESS_CTRL],
                      &st->state.num_sampler_views[PIPE_SHADER_TESS_CTRL]);
   }
}


static void
update_tesseval_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->TessEvalProgram._Current) {
      update_textures(st,
                      MESA_SHADER_TESS_EVAL,
                      &ctx->TessEvalProgram._Current->Base,
                      ctx->Const.Program[MESA_SHADER_TESS_EVAL].MaxTextureImageUnits,
                      st->state.sampler_views[PIPE_SHADER_TESS_EVAL],
                      &st->state.num_sampler_views[PIPE_SHADER_TESS_EVAL]);
   }
}


static void
update_compute_textures(struct st_context *st)
{
   const struct gl_context *ctx = st->ctx;

   if (ctx->ComputeProgram._Current) {
      update_textures(st,
                      MESA_SHADER_COMPUTE,
                      &ctx->ComputeProgram._Current->Base,
                      ctx->Const.Program[MESA_SHADER_COMPUTE].MaxTextureImageUnits,
                      st->state.sampler_views[PIPE_SHADER_COMPUTE],
                      &st->state.num_sampler_views[PIPE_SHADER_COMPUTE]);
   }
}


const struct st_tracked_state st_update_fragment_texture = {
   update_fragment_textures				/* update */
};


const struct st_tracked_state st_update_vertex_texture = {
   update_vertex_textures				/* update */
};


const struct st_tracked_state st_update_geometry_texture = {
   update_geometry_textures				/* update */
};


const struct st_tracked_state st_update_tessctrl_texture = {
   update_tessctrl_textures				/* update */
};


const struct st_tracked_state st_update_tesseval_texture = {
   update_tesseval_textures				/* update */
};


const struct st_tracked_state st_update_compute_texture = {
   update_compute_textures				/* update */
};
