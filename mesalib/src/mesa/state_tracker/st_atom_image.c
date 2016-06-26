/**************************************************************************
 *
 * Copyright 2016 Ilia Mirkin. All Rights Reserved.
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
#include "main/shaderimage.h"
#include "program/prog_parameter.h"
#include "program/prog_print.h"
#include "compiler/glsl/ir_uniform.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_surface.h"
#include "cso_cache/cso_context.h"

#include "st_cb_texture.h"
#include "st_debug.h"
#include "st_texture.h"
#include "st_context.h"
#include "st_atom.h"
#include "st_program.h"
#include "st_format.h"

static void
st_bind_images(struct st_context *st, struct gl_shader *shader,
              unsigned shader_type)
{
   unsigned i;
   struct pipe_image_view images[MAX_IMAGE_UNIFORMS];
   struct gl_program_constants *c;

   if (!shader || !st->pipe->set_shader_images)
      return;

   c = &st->ctx->Const.Program[shader->Stage];

   for (i = 0; i < shader->NumImages; i++) {
      struct gl_image_unit *u = &st->ctx->ImageUnits[shader->ImageUnits[i]];
      struct st_texture_object *stObj = st_texture_object(u->TexObj);
      struct pipe_image_view *img = &images[i];

      if (!_mesa_is_image_unit_valid(st->ctx, u) ||
          !st_finalize_texture(st->ctx, st->pipe, u->TexObj) ||
          !stObj->pt) {
         memset(img, 0, sizeof(*img));
         continue;
      }

      img->resource = stObj->pt;
      img->format = st_mesa_format_to_pipe_format(st, u->_ActualFormat);

      switch (u->Access) {
      case GL_READ_ONLY:
         img->access = PIPE_IMAGE_ACCESS_READ;
         break;
      case GL_WRITE_ONLY:
         img->access = PIPE_IMAGE_ACCESS_WRITE;
         break;
      case GL_READ_WRITE:
         img->access = PIPE_IMAGE_ACCESS_READ_WRITE;
         break;
      default:
         unreachable("bad gl_image_unit::Access");
      }

      if (stObj->pt->target == PIPE_BUFFER) {
         unsigned base, size;
         unsigned f, n;
         const struct util_format_description *desc
            = util_format_description(img->format);

         base = stObj->base.BufferOffset;
         assert(base < stObj->pt->width0);
         size = MIN2(stObj->pt->width0 - base, (unsigned)stObj->base.BufferSize);

         f = (base / (desc->block.bits / 8)) * desc->block.width;
         n = (size / (desc->block.bits / 8)) * desc->block.width;
         assert(n > 0);
         img->u.buf.first_element = f;
         img->u.buf.last_element  = f + (n - 1);
      } else {
         img->u.tex.level = u->Level + stObj->base.MinLevel;
         if (stObj->pt->target == PIPE_TEXTURE_3D) {
            if (u->Layered) {
               img->u.tex.first_layer = 0;
               img->u.tex.last_layer = u_minify(stObj->pt->depth0, img->u.tex.level) - 1;
            } else {
               img->u.tex.first_layer = u->_Layer;
               img->u.tex.last_layer = u->_Layer;
            }
         } else {
            img->u.tex.first_layer = u->_Layer + stObj->base.MinLayer;
            img->u.tex.last_layer = u->_Layer + stObj->base.MinLayer;
            if (u->Layered && img->resource->array_size > 1) {
               if (stObj->base.Immutable)
                  img->u.tex.last_layer += stObj->base.NumLayers - 1;
               else
                  img->u.tex.last_layer += img->resource->array_size - 1;
            }
         }
      }
   }
   cso_set_shader_images(st->cso_context, shader_type, 0, shader->NumImages,
                         images);
   /* clear out any stale shader images */
   if (shader->NumImages < c->MaxImageUniforms)
      cso_set_shader_images(
            st->cso_context, shader_type,
            shader->NumImages,
            c->MaxImageUniforms - shader->NumImages,
            NULL);
}

static void bind_vs_images(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_VERTEX];

   if (!prog)
      return;

   st_bind_images(st, prog->_LinkedShaders[MESA_SHADER_VERTEX], PIPE_SHADER_VERTEX);
}

const struct st_tracked_state st_bind_vs_images = {
   "st_bind_vs_images",
   {
      _NEW_TEXTURE,
      ST_NEW_VERTEX_PROGRAM | ST_NEW_IMAGE_UNITS,
   },
   bind_vs_images
};

static void bind_fs_images(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_FRAGMENT];

   if (!prog)
      return;

   st_bind_images(st, prog->_LinkedShaders[MESA_SHADER_FRAGMENT], PIPE_SHADER_FRAGMENT);
}

const struct st_tracked_state st_bind_fs_images = {
   "st_bind_fs_images",
   {
      _NEW_TEXTURE,
      ST_NEW_FRAGMENT_PROGRAM | ST_NEW_IMAGE_UNITS,
   },
   bind_fs_images
};

static void bind_gs_images(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY];

   if (!prog)
      return;

   st_bind_images(st, prog->_LinkedShaders[MESA_SHADER_GEOMETRY], PIPE_SHADER_GEOMETRY);
}

const struct st_tracked_state st_bind_gs_images = {
   "st_bind_gs_images",
   {
      _NEW_TEXTURE,
      ST_NEW_GEOMETRY_PROGRAM | ST_NEW_IMAGE_UNITS,
   },
   bind_gs_images
};

static void bind_tcs_images(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_CTRL];

   if (!prog)
      return;

   st_bind_images(st, prog->_LinkedShaders[MESA_SHADER_TESS_CTRL], PIPE_SHADER_TESS_CTRL);
}

const struct st_tracked_state st_bind_tcs_images = {
   "st_bind_tcs_images",
   {
      _NEW_TEXTURE,
      ST_NEW_TESSCTRL_PROGRAM | ST_NEW_IMAGE_UNITS,
   },
   bind_tcs_images
};

static void bind_tes_images(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];

   if (!prog)
      return;

   st_bind_images(st, prog->_LinkedShaders[MESA_SHADER_TESS_EVAL], PIPE_SHADER_TESS_EVAL);
}

const struct st_tracked_state st_bind_tes_images = {
   "st_bind_tes_images",
   {
      _NEW_TEXTURE,
      ST_NEW_TESSEVAL_PROGRAM | ST_NEW_IMAGE_UNITS,
   },
   bind_tes_images
};

static void bind_cs_images(struct st_context *st)
{
   struct gl_shader_program *prog =
      st->ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];

   if (!prog)
      return;

   st_bind_images(st, prog->_LinkedShaders[MESA_SHADER_COMPUTE], PIPE_SHADER_COMPUTE);
}

const struct st_tracked_state st_bind_cs_images = {
   "st_bind_cs_images",
   {
      _NEW_TEXTURE,
      ST_NEW_COMPUTE_PROGRAM | ST_NEW_IMAGE_UNITS,
   },
   bind_cs_images
};
