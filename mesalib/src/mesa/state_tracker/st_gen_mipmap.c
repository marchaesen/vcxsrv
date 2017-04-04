/**************************************************************************
 * 
 * Copyright 2008 VMware, Inc.
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
#include "main/mipmap.h"
#include "main/teximage.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_format.h"
#include "util/u_gen_mipmap.h"

#include "st_debug.h"
#include "st_context.h"
#include "st_texture.h"
#include "st_gen_mipmap.h"
#include "st_cb_bitmap.h"
#include "st_cb_texture.h"


/**
 * Compute the expected number of mipmap levels in the texture given
 * the width/height/depth of the base image and the GL_TEXTURE_BASE_LEVEL/
 * GL_TEXTURE_MAX_LEVEL settings.  This will tell us how many mipmap
 * levels should be generated.
 */
static GLuint
compute_num_levels(struct gl_context *ctx,
                   struct gl_texture_object *texObj,
                   GLenum target)
{
   const struct gl_texture_image *baseImage;
   GLuint numLevels;

   baseImage = _mesa_get_tex_image(ctx, texObj, target, texObj->BaseLevel);

   numLevels = texObj->BaseLevel + baseImage->MaxNumLevels;
   numLevels = MIN2(numLevels, (GLuint) texObj->MaxLevel + 1);
   if (texObj->Immutable)
      numLevels = MIN2(numLevels, texObj->NumLevels);
   assert(numLevels >= 1);

   return numLevels;
}


/**
 * Called via ctx->Driver.GenerateMipmap().
 */
void
st_generate_mipmap(struct gl_context *ctx, GLenum target,
                   struct gl_texture_object *texObj)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_object *stObj = st_texture_object(texObj);
   struct pipe_resource *pt = st_get_texobj_resource(texObj);
   const uint baseLevel = texObj->BaseLevel;
   enum pipe_format format;
   uint lastLevel, first_layer, last_layer;

   if (!pt)
      return;

   /* not sure if this ultimately actually should work,
      but we're not supporting multisampled textures yet. */
   assert(pt->nr_samples < 2);

   /* find expected last mipmap level to generate*/
   lastLevel = compute_num_levels(ctx, texObj, target) - 1;

   if (lastLevel == 0)
      return;

   st_flush_bitmap_cache(st);
   st_invalidate_readpix_cache(st);

   /* The texture isn't in a "complete" state yet so set the expected
    * lastLevel here, since it won't get done in st_finalize_texture().
    */
   stObj->lastLevel = lastLevel;

   if (!texObj->Immutable) {
      const GLboolean genSave = texObj->GenerateMipmap;

      /* Temporarily set GenerateMipmap to true so that allocate_full_mipmap()
       * makes the right decision about full mipmap allocation.
       */
      texObj->GenerateMipmap = GL_TRUE;

      _mesa_prepare_mipmap_levels(ctx, texObj, baseLevel, lastLevel);

      texObj->GenerateMipmap = genSave;

      /* At this point, memory for all the texture levels has been
       * allocated.  However, the base level image may be in one resource
       * while the subsequent/smaller levels may be in another resource.
       * Finalizing the texture will copy the base images from the former
       * resource to the latter.
       *
       * After this, we'll have all mipmap levels in one resource.
       */
      st_finalize_texture(ctx, st->pipe, texObj, 0);
   }

   pt = stObj->pt;
   if (!pt) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "mipmap generation");
      return;
   }

   assert(pt->last_level >= lastLevel);

   if (pt->target == PIPE_TEXTURE_CUBE) {
      first_layer = last_layer = _mesa_tex_target_to_face(target);
   }
   else {
      first_layer = 0;
      last_layer = util_max_layer(pt, baseLevel);
   }

   if (stObj->surface_based)
      format = stObj->surface_format;
   else
      format = pt->format;

   /* First see if the driver supports hardware mipmap generation,
    * if not then generate the mipmap by rendering/texturing.
    * If that fails, use the software fallback.
    */
   if (!st->pipe->screen->get_param(st->pipe->screen,
                                    PIPE_CAP_GENERATE_MIPMAP) ||
       !st->pipe->generate_mipmap(st->pipe, pt, format, baseLevel,
                                  lastLevel, first_layer, last_layer)) {

      if (!util_gen_mipmap(st->pipe, pt, format, baseLevel, lastLevel,
                           first_layer, last_layer, PIPE_TEX_FILTER_LINEAR)) {
         _mesa_generate_mipmap(ctx, target, texObj);
      }
   }
}
