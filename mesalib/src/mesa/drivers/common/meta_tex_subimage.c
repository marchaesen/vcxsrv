/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2015 Intel Corporation.  All Rights Reserved.
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
 *
 * Authors:
 *    Jason Ekstrand <jason.ekstrand@intel.com>
 */

#include "bufferobj.h"
#include "buffers.h"
#include "fbobject.h"
#include "glformats.h"
#include "glheader.h"
#include "image.h"
#include "macros.h"
#include "meta.h"
#include "pbo.h"
#include "shaderapi.h"
#include "state.h"
#include "teximage.h"
#include "texobj.h"
#include "texstate.h"
#include "uniforms.h"
#include "varray.h"

static struct gl_texture_image *
create_texture_for_pbo(struct gl_context *ctx, bool create_pbo,
                       GLenum pbo_target, int width, int height,
                       GLenum format, GLenum type, const void *pixels,
                       const struct gl_pixelstore_attrib *packing,
                       GLuint *tmp_pbo, GLuint *tmp_tex)
{
   uint32_t pbo_format;
   GLenum internal_format;
   unsigned row_stride;
   struct gl_buffer_object *buffer_obj;
   struct gl_texture_object *tex_obj;
   struct gl_texture_image *tex_image;
   bool read_only;

   if (packing->SwapBytes ||
       packing->LsbFirst ||
       packing->Invert)
      return NULL;

   pbo_format = _mesa_format_from_format_and_type(format, type);
   if (_mesa_format_is_mesa_array_format(pbo_format))
      pbo_format = _mesa_format_from_array_format(pbo_format);

   if (!pbo_format || !ctx->TextureFormatSupported[pbo_format])
      return NULL;

   /* Account for SKIP_PIXELS, SKIP_ROWS, ALIGNMENT, and SKIP_IMAGES */
   pixels = _mesa_image_address3d(packing, pixels,
                                  width, height, format, type, 0, 0, 0);
   row_stride = _mesa_image_row_stride(packing, width, format, type);

   if (_mesa_is_bufferobj(packing->BufferObj)) {
      *tmp_pbo = 0;
      buffer_obj = packing->BufferObj;
   } else {
      bool is_pixel_pack = pbo_target == GL_PIXEL_PACK_BUFFER;

      assert(create_pbo);

      _mesa_GenBuffers(1, tmp_pbo);

      /* We are not doing this inside meta_begin/end.  However, we know the
       * client doesn't have the given target bound, so we can go ahead and
       * squash it.  We'll set it back when we're done.
       */
      _mesa_BindBuffer(pbo_target, *tmp_pbo);

      /* In case of GL_PIXEL_PACK_BUFFER, pass null pointer for the pixel
       * data to avoid unnecessary data copying in _mesa_BufferData().
       */
      if (is_pixel_pack)
         _mesa_BufferData(pbo_target, row_stride * height, NULL,
                          GL_STREAM_READ);
      else
         _mesa_BufferData(pbo_target, row_stride * height, pixels,
                          GL_STREAM_DRAW);

      buffer_obj = packing->BufferObj;
      pixels = NULL;

      _mesa_BindBuffer(pbo_target, 0);
   }

   _mesa_GenTextures(1, tmp_tex);
   tex_obj = _mesa_lookup_texture(ctx, *tmp_tex);
   _mesa_initialize_texture_object(ctx, tex_obj, *tmp_tex, GL_TEXTURE_2D);
   /* This must be set after _mesa_initialize_texture_object, not before. */
   tex_obj->Immutable = GL_TRUE;
   /* This is required for interactions with ARB_texture_view. */
   tex_obj->NumLayers = 1;

   internal_format = _mesa_get_format_base_format(pbo_format);

   tex_image = _mesa_get_tex_image(ctx, tex_obj, tex_obj->Target, 0);
   _mesa_init_teximage_fields(ctx, tex_image, width, height, 1,
                              0, internal_format, pbo_format);

   read_only = pbo_target == GL_PIXEL_UNPACK_BUFFER;
   if (!ctx->Driver.SetTextureStorageForBufferObject(ctx, tex_obj,
                                                     buffer_obj,
                                                     (intptr_t)pixels,
                                                     row_stride,
                                                     read_only)) {
      _mesa_DeleteTextures(1, tmp_tex);
      _mesa_DeleteBuffers(1, tmp_pbo);
      return NULL;
   }

   return tex_image;
}

bool
_mesa_meta_pbo_TexSubImage(struct gl_context *ctx, GLuint dims,
                           struct gl_texture_image *tex_image,
                           int xoffset, int yoffset, int zoffset,
                           int width, int height, int depth,
                           GLenum format, GLenum type, const void *pixels,
                           bool allocate_storage, bool create_pbo,
                           const struct gl_pixelstore_attrib *packing)
{
   GLuint pbo = 0, pbo_tex = 0, fbos[2] = { 0, 0 };
   int full_height, image_height;
   struct gl_texture_image *pbo_tex_image;
   GLenum status;
   bool success = false;
   int z;

   if (!_mesa_is_bufferobj(packing->BufferObj) && !create_pbo)
      return false;

   if (format == GL_DEPTH_COMPONENT ||
       format == GL_DEPTH_STENCIL ||
       format == GL_STENCIL_INDEX ||
       format == GL_COLOR_INDEX)
      return false;

   if (ctx->_ImageTransferState)
      return false;

   /* For arrays, use a tall (height * depth) 2D texture but taking into
    * account the inter-image padding specified with the image height packing
    * property.
    */
   image_height = packing->ImageHeight == 0 ? height : packing->ImageHeight;
   full_height = image_height * (depth - 1) + height;

   pbo_tex_image = create_texture_for_pbo(ctx, create_pbo,
                                          GL_PIXEL_UNPACK_BUFFER,
                                          width, full_height,
                                          format, type, pixels, packing,
                                          &pbo, &pbo_tex);
   if (!pbo_tex_image)
      return false;

   if (allocate_storage)
      ctx->Driver.AllocTextureImageBuffer(ctx, tex_image);

   _mesa_meta_begin(ctx, ~(MESA_META_PIXEL_TRANSFER |
                           MESA_META_PIXEL_STORE));

   _mesa_GenFramebuffers(2, fbos);
   _mesa_BindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
   _mesa_BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);

   if (tex_image->TexObject->Target == GL_TEXTURE_1D_ARRAY) {
      assert(depth == 1);
      assert(zoffset == 0);
      depth = height;
      height = 1;
      image_height = 1;
      zoffset = yoffset;
      yoffset = 0;
   }

   _mesa_meta_bind_fbo_image(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             pbo_tex_image, 0);
   /* If this passes on the first layer it should pass on the others */
   status = _mesa_CheckFramebufferStatus(GL_READ_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      goto fail;

   _mesa_meta_bind_fbo_image(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             tex_image, zoffset);
   /* If this passes on the first layer it should pass on the others */
   status = _mesa_CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      goto fail;

   _mesa_update_state(ctx);

   if (_mesa_meta_BlitFramebuffer(ctx, ctx->ReadBuffer, ctx->DrawBuffer,
                                  0, 0, width, height,
                                  xoffset, yoffset,
                                  xoffset + width, yoffset + height,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST))
      goto fail;

   for (z = 1; z < depth; z++) {
      _mesa_meta_bind_fbo_image(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                tex_image, zoffset + z);

      _mesa_update_state(ctx);

      _mesa_meta_BlitFramebuffer(ctx, ctx->ReadBuffer, ctx->DrawBuffer,
                                 0, z * image_height,
                                 width, z * image_height + height,
                                 xoffset, yoffset,
                                 xoffset + width, yoffset + height,
                                 GL_COLOR_BUFFER_BIT, GL_NEAREST);
   }

   success = true;

fail:
   _mesa_DeleteFramebuffers(2, fbos);
   _mesa_DeleteTextures(1, &pbo_tex);
   _mesa_DeleteBuffers(1, &pbo);

   _mesa_meta_end(ctx);

   return success;
}

bool
_mesa_meta_pbo_GetTexSubImage(struct gl_context *ctx, GLuint dims,
                              struct gl_texture_image *tex_image,
                              int xoffset, int yoffset, int zoffset,
                              int width, int height, int depth,
                              GLenum format, GLenum type, const void *pixels,
                              const struct gl_pixelstore_attrib *packing)
{
   GLuint pbo = 0, pbo_tex = 0, fbos[2] = { 0, 0 };
   int full_height, image_height;
   struct gl_texture_image *pbo_tex_image;
   GLenum status;
   bool success = false;
   int z;

   if (!_mesa_is_bufferobj(packing->BufferObj))
      return false;

   if (format == GL_DEPTH_COMPONENT ||
       format == GL_DEPTH_STENCIL ||
       format == GL_STENCIL_INDEX ||
       format == GL_COLOR_INDEX)
      return false;

   if (ctx->_ImageTransferState)
      return false;

   /* For arrays, use a tall (height * depth) 2D texture but taking into
    * account the inter-image padding specified with the image height packing
    * property.
    */
   image_height = packing->ImageHeight == 0 ? height : packing->ImageHeight;
   full_height = image_height * (depth - 1) + height;

   pbo_tex_image = create_texture_for_pbo(ctx, false, GL_PIXEL_PACK_BUFFER,
                                          width, full_height * depth,
                                          format, type, pixels, packing,
                                          &pbo, &pbo_tex);
   if (!pbo_tex_image)
      return false;

   _mesa_meta_begin(ctx, ~(MESA_META_PIXEL_TRANSFER |
                           MESA_META_PIXEL_STORE));

   _mesa_GenFramebuffers(2, fbos);

   if (tex_image && tex_image->TexObject->Target == GL_TEXTURE_1D_ARRAY) {
      assert(depth == 1);
      assert(zoffset == 0);
      depth = height;
      height = 1;
      image_height = 1;
      zoffset = yoffset;
      yoffset = 0;
   }

   /* If we were given a texture, bind it to the read framebuffer.  If not,
    * we're doing a ReadPixels and we should just use whatever framebuffer
    * the client has bound.
    */
   if (tex_image) {
      _mesa_BindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
      _mesa_meta_bind_fbo_image(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                tex_image, zoffset);
      /* If this passes on the first layer it should pass on the others */
      status = _mesa_CheckFramebufferStatus(GL_READ_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE)
         goto fail;
   } else {
      assert(depth == 1);
   }

   _mesa_BindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
   _mesa_meta_bind_fbo_image(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                             pbo_tex_image, 0);
   /* If this passes on the first layer it should pass on the others */
   status = _mesa_CheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
   if (status != GL_FRAMEBUFFER_COMPLETE)
      goto fail;

   _mesa_update_state(ctx);

   if (_mesa_meta_BlitFramebuffer(ctx, ctx->ReadBuffer, ctx->DrawBuffer,
                                  xoffset, yoffset,
                                  xoffset + width, yoffset + height,
                                  0, 0, width, height,
                                  GL_COLOR_BUFFER_BIT, GL_NEAREST))
      goto fail;

   for (z = 1; z < depth; z++) {
      _mesa_meta_bind_fbo_image(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                tex_image, zoffset + z);

      _mesa_update_state(ctx);

      _mesa_meta_BlitFramebuffer(ctx, ctx->ReadBuffer, ctx->DrawBuffer,
                                 xoffset, yoffset,
                                 xoffset + width, yoffset + height,
                                 0, z * image_height,
                                 width, z * image_height + height,
                                 GL_COLOR_BUFFER_BIT, GL_NEAREST);
   }

   success = true;

fail:
   _mesa_DeleteFramebuffers(2, fbos);
   _mesa_DeleteTextures(1, &pbo_tex);
   _mesa_DeleteBuffers(1, &pbo);

   _mesa_meta_end(ctx);

   return success;
}
