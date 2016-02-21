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

#include <stdio.h>
#include "main/bufferobj.h"
#include "main/enums.h"
#include "main/fbobject.h"
#include "main/formats.h"
#include "main/format_utils.h"
#include "main/glformats.h"
#include "main/image.h"
#include "main/imports.h"
#include "main/macros.h"
#include "main/mipmap.h"
#include "main/pack.h"
#include "main/pbo.h"
#include "main/pixeltransfer.h"
#include "main/texcompress.h"
#include "main/texcompress_etc.h"
#include "main/texgetimage.h"
#include "main/teximage.h"
#include "main/texobj.h"
#include "main/texstore.h"

#include "state_tracker/st_debug.h"
#include "state_tracker/st_context.h"
#include "state_tracker/st_cb_fbo.h"
#include "state_tracker/st_cb_flush.h"
#include "state_tracker/st_cb_texture.h"
#include "state_tracker/st_cb_bufferobjects.h"
#include "state_tracker/st_format.h"
#include "state_tracker/st_texture.h"
#include "state_tracker/st_gen_mipmap.h"
#include "state_tracker/st_atom.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "util/u_inlines.h"
#include "util/u_upload_mgr.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_tile.h"
#include "util/u_format.h"
#include "util/u_surface.h"
#include "util/u_sampler.h"
#include "util/u_math.h"
#include "util/u_box.h"
#include "util/u_simple_shaders.h"
#include "cso_cache/cso_context.h"
#include "tgsi/tgsi_ureg.h"

#define DBG if (0) printf


enum pipe_texture_target
gl_target_to_pipe(GLenum target)
{
   switch (target) {
   case GL_TEXTURE_1D:
   case GL_PROXY_TEXTURE_1D:
      return PIPE_TEXTURE_1D;
   case GL_TEXTURE_2D:
   case GL_PROXY_TEXTURE_2D:
   case GL_TEXTURE_EXTERNAL_OES:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_PROXY_TEXTURE_2D_MULTISAMPLE:
      return PIPE_TEXTURE_2D;
   case GL_TEXTURE_RECTANGLE_NV:
   case GL_PROXY_TEXTURE_RECTANGLE_NV:
      return PIPE_TEXTURE_RECT;
   case GL_TEXTURE_3D:
   case GL_PROXY_TEXTURE_3D:
      return PIPE_TEXTURE_3D;
   case GL_TEXTURE_CUBE_MAP_ARB:
   case GL_PROXY_TEXTURE_CUBE_MAP_ARB:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      return PIPE_TEXTURE_CUBE;
   case GL_TEXTURE_1D_ARRAY_EXT:
   case GL_PROXY_TEXTURE_1D_ARRAY_EXT:
      return PIPE_TEXTURE_1D_ARRAY;
   case GL_TEXTURE_2D_ARRAY_EXT:
   case GL_PROXY_TEXTURE_2D_ARRAY_EXT:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
   case GL_PROXY_TEXTURE_2D_MULTISAMPLE_ARRAY:
      return PIPE_TEXTURE_2D_ARRAY;
   case GL_TEXTURE_BUFFER:
      return PIPE_BUFFER;
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_PROXY_TEXTURE_CUBE_MAP_ARRAY:
      return PIPE_TEXTURE_CUBE_ARRAY;
   default:
      assert(0);
      return 0;
   }
}


/** called via ctx->Driver.NewTextureImage() */
static struct gl_texture_image *
st_NewTextureImage(struct gl_context * ctx)
{
   DBG("%s\n", __func__);
   (void) ctx;
   return (struct gl_texture_image *) ST_CALLOC_STRUCT(st_texture_image);
}


/** called via ctx->Driver.DeleteTextureImage() */
static void
st_DeleteTextureImage(struct gl_context * ctx, struct gl_texture_image *img)
{
   /* nothing special (yet) for st_texture_image */
   _mesa_delete_texture_image(ctx, img);
}


/** called via ctx->Driver.NewTextureObject() */
static struct gl_texture_object *
st_NewTextureObject(struct gl_context * ctx, GLuint name, GLenum target)
{
   struct st_texture_object *obj = ST_CALLOC_STRUCT(st_texture_object);

   DBG("%s\n", __func__);
   _mesa_initialize_texture_object(ctx, &obj->base, name, target);

   return &obj->base;
}

/** called via ctx->Driver.DeleteTextureObject() */
static void 
st_DeleteTextureObject(struct gl_context *ctx,
                       struct gl_texture_object *texObj)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_object *stObj = st_texture_object(texObj);

   pipe_resource_reference(&stObj->pt, NULL);
   st_texture_release_all_sampler_views(st, stObj);
   st_texture_free_sampler_views(stObj);
   _mesa_delete_texture_object(ctx, texObj);
}


/** called via ctx->Driver.FreeTextureImageBuffer() */
static void
st_FreeTextureImageBuffer(struct gl_context *ctx,
                          struct gl_texture_image *texImage)
{
   struct st_texture_image *stImage = st_texture_image(texImage);

   DBG("%s\n", __func__);

   if (stImage->pt) {
      pipe_resource_reference(&stImage->pt, NULL);
   }

   free(stImage->transfer);
   stImage->transfer = NULL;
   stImage->num_transfers = 0;
}


/** called via ctx->Driver.MapTextureImage() */
static void
st_MapTextureImage(struct gl_context *ctx,
                   struct gl_texture_image *texImage,
                   GLuint slice, GLuint x, GLuint y, GLuint w, GLuint h,
                   GLbitfield mode,
                   GLubyte **mapOut, GLint *rowStrideOut)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_image *stImage = st_texture_image(texImage);
   unsigned pipeMode;
   GLubyte *map;
   struct pipe_transfer *transfer;

   pipeMode = 0x0;
   if (mode & GL_MAP_READ_BIT)
      pipeMode |= PIPE_TRANSFER_READ;
   if (mode & GL_MAP_WRITE_BIT)
      pipeMode |= PIPE_TRANSFER_WRITE;
   if (mode & GL_MAP_INVALIDATE_RANGE_BIT)
      pipeMode |= PIPE_TRANSFER_DISCARD_RANGE;

   map = st_texture_image_map(st, stImage, pipeMode, x, y, slice, w, h, 1,
                              &transfer);
   if (map) {
      if ((_mesa_is_format_etc2(texImage->TexFormat) && !st->has_etc2) ||
          (texImage->TexFormat == MESA_FORMAT_ETC1_RGB8 && !st->has_etc1)) {
         /* ETC isn't supported by gallium and it's represented
          * by uncompressed formats. Only write transfers with precompressed
          * data are supported by ES3, which makes this really simple.
          *
          * Just create a temporary storage where the ETC texture will
          * be stored. It will be decompressed in the Unmap function.
          */
         unsigned z = transfer->box.z;
         struct st_texture_image_transfer *itransfer = &stImage->transfer[z];

         itransfer->temp_data =
            malloc(_mesa_format_image_size(texImage->TexFormat, w, h, 1));
         itransfer->temp_stride =
            _mesa_format_row_stride(texImage->TexFormat, w);
         itransfer->map = map;

         *mapOut = itransfer->temp_data;
         *rowStrideOut = itransfer->temp_stride;
      }
      else {
         /* supported mapping */
         *mapOut = map;
         *rowStrideOut = transfer->stride;
      }
   }
   else {
      *mapOut = NULL;
      *rowStrideOut = 0;
   }
}


/** called via ctx->Driver.UnmapTextureImage() */
static void
st_UnmapTextureImage(struct gl_context *ctx,
                     struct gl_texture_image *texImage,
                     GLuint slice)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_image *stImage  = st_texture_image(texImage);

   if ((_mesa_is_format_etc2(texImage->TexFormat) && !st->has_etc2) ||
       (texImage->TexFormat == MESA_FORMAT_ETC1_RGB8 && !st->has_etc1)) {
      /* Decompress the ETC texture to the mapped one. */
      unsigned z = slice + stImage->base.Face;
      struct st_texture_image_transfer *itransfer = &stImage->transfer[z];
      struct pipe_transfer *transfer = itransfer->transfer;

      assert(z == transfer->box.z);

      if (texImage->TexFormat == MESA_FORMAT_ETC1_RGB8) {
         _mesa_etc1_unpack_rgba8888(itransfer->map, transfer->stride,
                                    itransfer->temp_data,
                                    itransfer->temp_stride,
                                    transfer->box.width, transfer->box.height);
      }
      else {
         _mesa_unpack_etc2_format(itransfer->map, transfer->stride,
                                  itransfer->temp_data, itransfer->temp_stride,
                                  transfer->box.width, transfer->box.height,
                                  texImage->TexFormat);
      }

      free(itransfer->temp_data);
      itransfer->temp_data = NULL;
      itransfer->temp_stride = 0;
      itransfer->map = 0;
   }

   st_texture_image_unmap(st, stImage, slice);
}


/**
 * Return default texture resource binding bitmask for the given format.
 */
static GLuint
default_bindings(struct st_context *st, enum pipe_format format)
{
   struct pipe_screen *screen = st->pipe->screen;
   const unsigned target = PIPE_TEXTURE_2D;
   unsigned bindings;

   if (util_format_is_depth_or_stencil(format))
      bindings = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_DEPTH_STENCIL;
   else
      bindings = PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET;

   if (screen->is_format_supported(screen, format, target, 0, bindings))
      return bindings;
   else {
      /* Try non-sRGB. */
      format = util_format_linear(format);

      if (screen->is_format_supported(screen, format, target, 0, bindings))
         return bindings;
      else
         return PIPE_BIND_SAMPLER_VIEW;
   }
}


/**
 * Given the size of a mipmap image, try to compute the size of the level=0
 * mipmap image.
 *
 * Note that this isn't always accurate for odd-sized, non-POW textures.
 * For example, if level=1 and width=40 then the level=0 width may be 80 or 81.
 *
 * \return GL_TRUE for success, GL_FALSE for failure
 */
static GLboolean
guess_base_level_size(GLenum target,
                      GLuint width, GLuint height, GLuint depth, GLuint level,
                      GLuint *width0, GLuint *height0, GLuint *depth0)
{ 
   assert(width >= 1);
   assert(height >= 1);
   assert(depth >= 1);

   if (level > 0) {
      /* Guess the size of the base level.
       * Depending on the image's size, we can't always make a guess here.
       */
      switch (target) {
      case GL_TEXTURE_1D:
      case GL_TEXTURE_1D_ARRAY:
         width <<= level;
         break;

      case GL_TEXTURE_2D:
      case GL_TEXTURE_2D_ARRAY:
         /* We can't make a good guess here, because the base level dimensions
          * can be non-square.
          */
         if (width == 1 || height == 1) {
            return GL_FALSE;
         }
         width <<= level;
         height <<= level;
         break;

      case GL_TEXTURE_CUBE_MAP:
      case GL_TEXTURE_CUBE_MAP_ARRAY:
         width <<= level;
         height <<= level;
         break;

      case GL_TEXTURE_3D:
         /* We can't make a good guess here, because the base level dimensions
          * can be non-cube.
          */
         if (width == 1 || height == 1 || depth == 1) {
            return GL_FALSE;
         }
         width <<= level;
         height <<= level;
         depth <<= level;
         break;

      case GL_TEXTURE_RECTANGLE:
         break;

      default:
         assert(0);
      }
   }

   *width0 = width;
   *height0 = height;
   *depth0 = depth;

   return GL_TRUE;
}


/**
 * Try to determine whether we should allocate memory for a full texture
 * mipmap.  The problem is when we get a glTexImage(level=0) call, we
 * can't immediately know if other mipmap levels are coming next.  Here
 * we try to guess whether to allocate memory for a mipmap or just the
 * 0th level.
 *
 * If we guess incorrectly here we'll later reallocate the right amount of
 * memory either in st_AllocTextureImageBuffer() or st_finalize_texture().
 *
 * \param stObj  the texture object we're going to allocate memory for.
 * \param stImage  describes the incoming image which we need to store.
 */
static boolean
allocate_full_mipmap(const struct st_texture_object *stObj,
                     const struct st_texture_image *stImage)
{
   switch (stObj->base.Target) {
   case GL_TEXTURE_RECTANGLE_NV:
   case GL_TEXTURE_BUFFER:
   case GL_TEXTURE_EXTERNAL_OES:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      /* these texture types cannot be mipmapped */
      return FALSE;
   }

   if (stImage->base.Level > 0 || stObj->base.GenerateMipmap)
      return TRUE;

   if (stImage->base._BaseFormat == GL_DEPTH_COMPONENT ||
       stImage->base._BaseFormat == GL_DEPTH_STENCIL_EXT)
      /* depth/stencil textures are seldom mipmapped */
      return FALSE;

   if (stObj->base.BaseLevel == 0 && stObj->base.MaxLevel == 0)
      return FALSE;

   if (stObj->base.Sampler.MinFilter == GL_NEAREST ||
       stObj->base.Sampler.MinFilter == GL_LINEAR)
      /* not a mipmap minification filter */
      return FALSE;

   if (stObj->base.Target == GL_TEXTURE_3D)
      /* 3D textures are seldom mipmapped */
      return FALSE;

   return TRUE;
}


/**
 * Try to allocate a pipe_resource object for the given st_texture_object.
 *
 * We use the given st_texture_image as a clue to determine the size of the
 * mipmap image at level=0.
 *
 * \return GL_TRUE for success, GL_FALSE if out of memory.
 */
static GLboolean
guess_and_alloc_texture(struct st_context *st,
			struct st_texture_object *stObj,
			const struct st_texture_image *stImage)
{
   GLuint lastLevel, width, height, depth;
   GLuint bindings;
   GLuint ptWidth, ptHeight, ptDepth, ptLayers;
   enum pipe_format fmt;

   DBG("%s\n", __func__);

   assert(!stObj->pt);

   if (!guess_base_level_size(stObj->base.Target,
                              stImage->base.Width2,
                              stImage->base.Height2,
                              stImage->base.Depth2,
                              stImage->base.Level,
                              &width, &height, &depth)) {
      /* we can't determine the image size at level=0 */
      stObj->width0 = stObj->height0 = stObj->depth0 = 0;
      /* this is not an out of memory error */
      return GL_TRUE;
   }

   /* At this point, (width x height x depth) is the expected size of
    * the level=0 mipmap image.
    */

   /* Guess a reasonable value for lastLevel.  With OpenGL we have no
    * idea how many mipmap levels will be in a texture until we start
    * to render with it.  Make an educated guess here but be prepared
    * to re-allocating a texture buffer with space for more (or fewer)
    * mipmap levels later.
    */
   if (allocate_full_mipmap(stObj, stImage)) {
      /* alloc space for a full mipmap */
      lastLevel = _mesa_get_tex_max_num_levels(stObj->base.Target,
                                               width, height, depth) - 1;
   }
   else {
      /* only alloc space for a single mipmap level */
      lastLevel = 0;
   }

   /* Save the level=0 dimensions */
   stObj->width0 = width;
   stObj->height0 = height;
   stObj->depth0 = depth;

   fmt = st_mesa_format_to_pipe_format(st, stImage->base.TexFormat);

   bindings = default_bindings(st, fmt);

   st_gl_texture_dims_to_pipe_dims(stObj->base.Target,
                                   width, height, depth,
                                   &ptWidth, &ptHeight, &ptDepth, &ptLayers);

   stObj->pt = st_texture_create(st,
                                 gl_target_to_pipe(stObj->base.Target),
                                 fmt,
                                 lastLevel,
                                 ptWidth,
                                 ptHeight,
                                 ptDepth,
                                 ptLayers, 0,
                                 bindings);

   stObj->lastLevel = lastLevel;

   DBG("%s returning %d\n", __func__, (stObj->pt != NULL));

   return stObj->pt != NULL;
}


/**
 * Called via ctx->Driver.AllocTextureImageBuffer().
 * If the texture object/buffer already has space for the indicated image,
 * we're done.  Otherwise, allocate memory for the new texture image.
 */
static GLboolean
st_AllocTextureImageBuffer(struct gl_context *ctx,
                           struct gl_texture_image *texImage)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct st_texture_object *stObj = st_texture_object(texImage->TexObject);
   const GLuint level = texImage->Level;
   GLuint width = texImage->Width;
   GLuint height = texImage->Height;
   GLuint depth = texImage->Depth;

   DBG("%s\n", __func__);

   assert(!stImage->pt); /* xxx this might be wrong */

   /* Look if the parent texture object has space for this image */
   if (stObj->pt &&
       level <= stObj->pt->last_level &&
       st_texture_match_image(st, stObj->pt, texImage)) {
      /* this image will fit in the existing texture object's memory */
      pipe_resource_reference(&stImage->pt, stObj->pt);
      return GL_TRUE;
   }

   /* The parent texture object does not have space for this image */

   pipe_resource_reference(&stObj->pt, NULL);
   st_texture_release_all_sampler_views(st, stObj);

   if (!guess_and_alloc_texture(st, stObj, stImage)) {
      /* Probably out of memory.
       * Try flushing any pending rendering, then retry.
       */
      st_finish(st);
      if (!guess_and_alloc_texture(st, stObj, stImage)) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage");
         return GL_FALSE;
      }
   }

   if (stObj->pt &&
       st_texture_match_image(st, stObj->pt, texImage)) {
      /* The image will live in the object's mipmap memory */
      pipe_resource_reference(&stImage->pt, stObj->pt);
      assert(stImage->pt);
      return GL_TRUE;
   }
   else {
      /* Create a new, temporary texture/resource/buffer to hold this
       * one texture image.  Note that when we later access this image
       * (either for mapping or copying) we'll want to always specify
       * mipmap level=0, even if the image represents some other mipmap
       * level.
       */
      enum pipe_format format =
         st_mesa_format_to_pipe_format(st, texImage->TexFormat);
      GLuint bindings = default_bindings(st, format);
      GLuint ptWidth, ptHeight, ptDepth, ptLayers;

      st_gl_texture_dims_to_pipe_dims(stObj->base.Target,
                                      width, height, depth,
                                      &ptWidth, &ptHeight, &ptDepth, &ptLayers);

      stImage->pt = st_texture_create(st,
                                      gl_target_to_pipe(stObj->base.Target),
                                      format,
                                      0, /* lastLevel */
                                      ptWidth,
                                      ptHeight,
                                      ptDepth,
                                      ptLayers, 0,
                                      bindings);
      return stImage->pt != NULL;
   }
}


/**
 * Preparation prior to glTexImage.  Basically check the 'surface_based'
 * field and switch to a "normal" tex image if necessary.
 */
static void
prep_teximage(struct gl_context *ctx, struct gl_texture_image *texImage,
              GLenum format, GLenum type)
{
   struct gl_texture_object *texObj = texImage->TexObject;
   struct st_texture_object *stObj = st_texture_object(texObj);

   /* switch to "normal" */
   if (stObj->surface_based) {
      const GLenum target = texObj->Target;
      const GLuint level = texImage->Level;
      mesa_format texFormat;

      _mesa_clear_texture_object(ctx, texObj);
      pipe_resource_reference(&stObj->pt, NULL);

      /* oops, need to init this image again */
      texFormat = _mesa_choose_texture_format(ctx, texObj, target, level,
                                              texImage->InternalFormat, format,
                                              type);

      _mesa_init_teximage_fields(ctx, texImage,
                                 texImage->Width, texImage->Height,
                                 texImage->Depth, texImage->Border,
                                 texImage->InternalFormat, texFormat);

      stObj->surface_based = GL_FALSE;
   }
}


/**
 * Return a writemask for the gallium blit. The parameters can be base
 * formats or "format" from glDrawPixels/glTexImage/glGetTexImage.
 */
unsigned
st_get_blit_mask(GLenum srcFormat, GLenum dstFormat)
{
   switch (dstFormat) {
   case GL_DEPTH_STENCIL:
      switch (srcFormat) {
      case GL_DEPTH_STENCIL:
         return PIPE_MASK_ZS;
      case GL_DEPTH_COMPONENT:
         return PIPE_MASK_Z;
      case GL_STENCIL_INDEX:
         return PIPE_MASK_S;
      default:
         assert(0);
         return 0;
      }

   case GL_DEPTH_COMPONENT:
      switch (srcFormat) {
      case GL_DEPTH_STENCIL:
      case GL_DEPTH_COMPONENT:
         return PIPE_MASK_Z;
      default:
         assert(0);
         return 0;
      }

   case GL_STENCIL_INDEX:
      switch (srcFormat) {
      case GL_STENCIL_INDEX:
         return PIPE_MASK_S;
      default:
         assert(0);
         return 0;
      }

   default:
      return PIPE_MASK_RGBA;
   }
}

void
st_init_pbo_upload(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;

   st->pbo_upload.enabled =
      screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OBJECTS) &&
      screen->get_param(screen, PIPE_CAP_TEXTURE_BUFFER_OFFSET_ALIGNMENT) >= 1 &&
      screen->get_shader_param(screen, PIPE_SHADER_FRAGMENT, PIPE_SHADER_CAP_INTEGERS);
   if (!st->pbo_upload.enabled)
      return;

   st->pbo_upload.rgba_only =
      screen->get_param(screen, PIPE_CAP_BUFFER_SAMPLER_VIEW_RGBA_ONLY);

   if (screen->get_param(screen, PIPE_CAP_TGSI_INSTANCEID)) {
      if (screen->get_param(screen, PIPE_CAP_TGSI_VS_LAYER_VIEWPORT)) {
         st->pbo_upload.upload_layers = true;
      } else if (screen->get_param(screen, PIPE_CAP_MAX_GEOMETRY_OUTPUT_VERTICES) >= 3) {
         st->pbo_upload.upload_layers = true;
         st->pbo_upload.use_gs = true;
      }
   }

   /* Blend state */
   memset(&st->pbo_upload.blend, 0, sizeof(struct pipe_blend_state));
   st->pbo_upload.blend.rt[0].colormask = PIPE_MASK_RGBA;

   /* Rasterizer state */
   memset(&st->pbo_upload.raster, 0, sizeof(struct pipe_rasterizer_state));
   st->pbo_upload.raster.half_pixel_center = 1;
}

void
st_destroy_pbo_upload(struct st_context *st)
{
   if (st->pbo_upload.fs) {
      cso_delete_fragment_shader(st->cso_context, st->pbo_upload.fs);
      st->pbo_upload.fs = NULL;
   }

   if (st->pbo_upload.gs) {
      cso_delete_geometry_shader(st->cso_context, st->pbo_upload.gs);
      st->pbo_upload.gs = NULL;
   }

   if (st->pbo_upload.vs) {
      cso_delete_vertex_shader(st->cso_context, st->pbo_upload.vs);
      st->pbo_upload.vs = NULL;
   }
}

/**
 * Converts format to a format with the same components, types
 * and sizes, but with the components in RGBA order.
 */
static enum pipe_format
unswizzle_format(enum pipe_format format)
{
   switch (format)
   {
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_A8R8G8B8_UNORM:
   case PIPE_FORMAT_A8B8G8R8_UNORM:
      return PIPE_FORMAT_R8G8B8A8_UNORM;

   case PIPE_FORMAT_B10G10R10A2_UNORM:
      return PIPE_FORMAT_R10G10B10A2_UNORM;

   case PIPE_FORMAT_B10G10R10A2_SNORM:
      return PIPE_FORMAT_R10G10B10A2_SNORM;

   case PIPE_FORMAT_B10G10R10A2_UINT:
      return PIPE_FORMAT_R10G10B10A2_UINT;

   default:
      return format;
   }
}

/**
 * Converts PIPE_FORMAT_A* to PIPE_FORMAT_R*.
 */
static enum pipe_format
alpha_to_red(enum pipe_format format)
{
   switch (format)
   {
   case PIPE_FORMAT_A8_UNORM:
      return PIPE_FORMAT_R8_UNORM;
   case PIPE_FORMAT_A8_SNORM:
      return PIPE_FORMAT_R8_SNORM;
   case PIPE_FORMAT_A8_UINT:
      return PIPE_FORMAT_R8_UINT;
   case PIPE_FORMAT_A8_SINT:
      return PIPE_FORMAT_R8_SINT;

   case PIPE_FORMAT_A16_UNORM:
      return PIPE_FORMAT_R16_UNORM;
   case PIPE_FORMAT_A16_SNORM:
      return PIPE_FORMAT_R16_SNORM;
   case PIPE_FORMAT_A16_UINT:
      return PIPE_FORMAT_R16_UINT;
   case PIPE_FORMAT_A16_SINT:
      return PIPE_FORMAT_R16_SINT;
   case PIPE_FORMAT_A16_FLOAT:
      return PIPE_FORMAT_R16_FLOAT;

   case PIPE_FORMAT_A32_UINT:
      return PIPE_FORMAT_R32_UINT;
   case PIPE_FORMAT_A32_SINT:
      return PIPE_FORMAT_R32_SINT;
   case PIPE_FORMAT_A32_FLOAT:
      return PIPE_FORMAT_R32_FLOAT;

   default:
      return format;
   }
}

/**
 * Converts PIPE_FORMAT_R*A* to PIPE_FORMAT_R*G*.
 */
static enum pipe_format
red_alpha_to_red_green(enum pipe_format format)
{
   switch (format)
   {
   case PIPE_FORMAT_R8A8_UNORM:
      return PIPE_FORMAT_R8G8_UNORM;
   case PIPE_FORMAT_R8A8_SNORM:
      return PIPE_FORMAT_R8G8_SNORM;
   case PIPE_FORMAT_R8A8_UINT:
      return PIPE_FORMAT_R8G8_UINT;
   case PIPE_FORMAT_R8A8_SINT:
      return PIPE_FORMAT_R8G8_SINT;

   case PIPE_FORMAT_R16A16_UNORM:
      return PIPE_FORMAT_R16G16_UNORM;
   case PIPE_FORMAT_R16A16_SNORM:
      return PIPE_FORMAT_R16G16_SNORM;
   case PIPE_FORMAT_R16A16_UINT:
      return PIPE_FORMAT_R16G16_UINT;
   case PIPE_FORMAT_R16A16_SINT:
      return PIPE_FORMAT_R16G16_SINT;
   case PIPE_FORMAT_R16A16_FLOAT:
      return PIPE_FORMAT_R16G16_FLOAT;

   case PIPE_FORMAT_R32A32_UINT:
      return PIPE_FORMAT_R32G32_UINT;
   case PIPE_FORMAT_R32A32_SINT:
      return PIPE_FORMAT_R32G32_SINT;
   case PIPE_FORMAT_R32A32_FLOAT:
      return PIPE_FORMAT_R32G32_FLOAT;

   default:
       return format;
   }
}

/**
 * Converts PIPE_FORMAT_L*A* to PIPE_FORMAT_R*G*.
 */
static enum pipe_format
luminance_alpha_to_red_green(enum pipe_format format)
{
   switch (format)
   {
   case PIPE_FORMAT_L8A8_UNORM:
      return PIPE_FORMAT_R8G8_UNORM;
   case PIPE_FORMAT_L8A8_SNORM:
      return PIPE_FORMAT_R8G8_SNORM;
   case PIPE_FORMAT_L8A8_UINT:
      return PIPE_FORMAT_R8G8_UINT;
   case PIPE_FORMAT_L8A8_SINT:
      return PIPE_FORMAT_R8G8_SINT;

   case PIPE_FORMAT_L16A16_UNORM:
      return PIPE_FORMAT_R16G16_UNORM;
   case PIPE_FORMAT_L16A16_SNORM:
      return PIPE_FORMAT_R16G16_SNORM;
   case PIPE_FORMAT_L16A16_UINT:
      return PIPE_FORMAT_R16G16_UINT;
   case PIPE_FORMAT_L16A16_SINT:
      return PIPE_FORMAT_R16G16_SINT;
   case PIPE_FORMAT_L16A16_FLOAT:
      return PIPE_FORMAT_R16G16_FLOAT;

   case PIPE_FORMAT_L32A32_UINT:
      return PIPE_FORMAT_R32G32_UINT;
   case PIPE_FORMAT_L32A32_SINT:
      return PIPE_FORMAT_R32G32_SINT;
   case PIPE_FORMAT_L32A32_FLOAT:
      return PIPE_FORMAT_R32G32_FLOAT;

   default:
       return format;
   }
}

/**
 * Returns true if format is a PIPE_FORMAT_A* format, and false otherwise.
 */
static bool
format_is_alpha(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   if (desc->nr_channels == 1 &&
       desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[1] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[2] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_X)
      return true;

   return false;
}

/**
 * Returns true if format is a PIPE_FORMAT_R* format, and false otherwise.
 */
static bool
format_is_red(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   if (desc->nr_channels == 1 &&
       desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_X &&
       desc->swizzle[1] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[2] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_1)
      return true;

   return false;
}


/**
 * Returns true if format is a PIPE_FORMAT_L* format, and false otherwise.
 */
static bool
format_is_luminance(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   if (desc->nr_channels == 1 &&
       desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_X &&
       desc->swizzle[1] == UTIL_FORMAT_SWIZZLE_X &&
       desc->swizzle[2] == UTIL_FORMAT_SWIZZLE_X &&
       desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_1)
      return true;

   return false;
}

/**
 * Returns true if format is a PIPE_FORMAT_R*A* format, and false otherwise.
 */
static bool
format_is_red_alpha(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   if (desc->nr_channels == 2 &&
       desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_X &&
       desc->swizzle[1] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[2] == UTIL_FORMAT_SWIZZLE_0 &&
       desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_Y)
      return true;

   return false;
}

static bool
format_is_swizzled_rgba(enum pipe_format format)
{
    const struct util_format_description *desc = util_format_description(format);

    if ((desc->swizzle[0] == TGSI_SWIZZLE_X || desc->swizzle[0] == UTIL_FORMAT_SWIZZLE_0) &&
        (desc->swizzle[1] == TGSI_SWIZZLE_Y || desc->swizzle[1] == UTIL_FORMAT_SWIZZLE_0) &&
        (desc->swizzle[2] == TGSI_SWIZZLE_Z || desc->swizzle[2] == UTIL_FORMAT_SWIZZLE_0) &&
        (desc->swizzle[3] == TGSI_SWIZZLE_W || desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_1))
       return false;

    return true;
}

struct format_table
{
   unsigned char swizzle[4];
   enum pipe_format format;
};

static const struct format_table table_8888_unorm[] = {
   { { 0, 1, 2, 3 }, PIPE_FORMAT_R8G8B8A8_UNORM },
   { { 2, 1, 0, 3 }, PIPE_FORMAT_B8G8R8A8_UNORM },
   { { 3, 0, 1, 2 }, PIPE_FORMAT_A8R8G8B8_UNORM },
   { { 3, 2, 1, 0 }, PIPE_FORMAT_A8B8G8R8_UNORM }
};

static const struct format_table table_1010102_unorm[] = {
   { { 0, 1, 2, 3 }, PIPE_FORMAT_R10G10B10A2_UNORM },
   { { 2, 1, 0, 3 }, PIPE_FORMAT_B10G10R10A2_UNORM }
};

static const struct format_table table_1010102_snorm[] = {
   { { 0, 1, 2, 3 }, PIPE_FORMAT_R10G10B10A2_SNORM },
   { { 2, 1, 0, 3 }, PIPE_FORMAT_B10G10R10A2_SNORM }
};

static const struct format_table table_1010102_uint[] = {
   { { 0, 1, 2, 3 }, PIPE_FORMAT_R10G10B10A2_UINT },
   { { 2, 1, 0, 3 }, PIPE_FORMAT_B10G10R10A2_UINT }
};

static enum pipe_format
swizzle_format(enum pipe_format format, const int * const swizzle)
{
   unsigned i;

   switch (format) {
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_A8R8G8B8_UNORM:
   case PIPE_FORMAT_A8B8G8R8_UNORM:
      for (i = 0; i < ARRAY_SIZE(table_8888_unorm); i++) {
         if (swizzle[0] == table_8888_unorm[i].swizzle[0] &&
             swizzle[1] == table_8888_unorm[i].swizzle[1] &&
             swizzle[2] == table_8888_unorm[i].swizzle[2] &&
             swizzle[3] == table_8888_unorm[i].swizzle[3])
            return table_8888_unorm[i].format;
      }
      break;

   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_B10G10R10A2_UNORM:
      for (i = 0; i < ARRAY_SIZE(table_1010102_unorm); i++) {
         if (swizzle[0] == table_1010102_unorm[i].swizzle[0] &&
             swizzle[1] == table_1010102_unorm[i].swizzle[1] &&
             swizzle[2] == table_1010102_unorm[i].swizzle[2] &&
             swizzle[3] == table_1010102_unorm[i].swizzle[3])
            return table_1010102_unorm[i].format;
      }
      break;

   case PIPE_FORMAT_R10G10B10A2_SNORM:
   case PIPE_FORMAT_B10G10R10A2_SNORM:
      for (i = 0; i < ARRAY_SIZE(table_1010102_snorm); i++) {
         if (swizzle[0] == table_1010102_snorm[i].swizzle[0] &&
             swizzle[1] == table_1010102_snorm[i].swizzle[1] &&
             swizzle[2] == table_1010102_snorm[i].swizzle[2] &&
             swizzle[3] == table_1010102_snorm[i].swizzle[3])
            return table_1010102_snorm[i].format;
      }
      break;

   case PIPE_FORMAT_R10G10B10A2_UINT:
   case PIPE_FORMAT_B10G10R10A2_UINT:
      for (i = 0; i < ARRAY_SIZE(table_1010102_uint); i++) {
         if (swizzle[0] == table_1010102_uint[i].swizzle[0] &&
             swizzle[1] == table_1010102_uint[i].swizzle[1] &&
             swizzle[2] == table_1010102_uint[i].swizzle[2] &&
             swizzle[3] == table_1010102_uint[i].swizzle[3])
            return table_1010102_uint[i].format;
      }
      break;

   default:
      break;
   }

   return PIPE_FORMAT_NONE;
}

static bool
reinterpret_formats(enum pipe_format *src_format, enum pipe_format *dst_format)
{
   enum pipe_format src = *src_format;
   enum pipe_format dst = *dst_format;

   /* Note: dst_format has already been transformed from luminance/intensity
    *       to red when this function is called.  The source format will never
    *       be an intensity format, because GL_INTENSITY is not a legal value
    *       for the format parameter in glTex(Sub)Image(). */

   if (format_is_alpha(src)) {
      if (!format_is_alpha(dst))
         return false;

      src = alpha_to_red(src);
      dst = alpha_to_red(dst);
   } else if (format_is_luminance(src)) {
      if (!format_is_red(dst) && !format_is_red_alpha(dst))
         return false;

      src = util_format_luminance_to_red(src);
   } else if (util_format_is_luminance_alpha(src)) {
      src = luminance_alpha_to_red_green(src);

      if (format_is_red_alpha(dst)) {
         dst = red_alpha_to_red_green(dst);
      } else if (!format_is_red(dst))
         return false;
   } else if (format_is_swizzled_rgba(src)) {
      const struct util_format_description *src_desc = util_format_description(src);
      const struct util_format_description *dst_desc = util_format_description(dst);
      int swizzle[4];
      unsigned i;

      /* Make sure the format is an RGBA and not an RGBX format */
      if (src_desc->nr_channels != 4 || src_desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_1)
         return false;

      if (dst_desc->nr_channels != 4 || dst_desc->swizzle[3] == UTIL_FORMAT_SWIZZLE_1)
         return false;

      for (i = 0; i < 4; i++)
         swizzle[i] = dst_desc->swizzle[src_desc->swizzle[i]];

      dst = swizzle_format(dst, swizzle);
      if (dst == PIPE_FORMAT_NONE)
         return false;

      src = unswizzle_format(src);
   }

   *src_format = src;
   *dst_format = dst;
   return true;
}

static void *
create_pbo_upload_vs(struct st_context *st)
{
   struct ureg_program *ureg;
   struct ureg_src in_pos;
   struct ureg_src in_instanceid;
   struct ureg_dst out_pos;
   struct ureg_dst out_layer;

   ureg = ureg_create(TGSI_PROCESSOR_VERTEX);
   if (!ureg)
      return NULL;

   in_pos = ureg_DECL_vs_input(ureg, TGSI_SEMANTIC_POSITION);

   out_pos = ureg_DECL_output(ureg, TGSI_SEMANTIC_POSITION, 0);

   if (st->pbo_upload.upload_layers) {
      in_instanceid = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_INSTANCEID, 0);

      if (!st->pbo_upload.use_gs)
         out_layer = ureg_DECL_output(ureg, TGSI_SEMANTIC_LAYER, 0);
   }

   /* out_pos = in_pos */
   ureg_MOV(ureg, out_pos, in_pos);

   if (st->pbo_upload.upload_layers) {
      if (st->pbo_upload.use_gs) {
         /* out_pos.z = i2f(gl_InstanceID) */
         ureg_I2F(ureg, ureg_writemask(out_pos, TGSI_WRITEMASK_Z),
                        ureg_scalar(in_instanceid, TGSI_SWIZZLE_X));
      } else {
         /* out_layer = gl_InstanceID */
         ureg_MOV(ureg, out_layer, in_instanceid);
      }
   }

   ureg_END(ureg);

   return ureg_create_shader_and_destroy(ureg, st->pipe);
}

static void *
create_pbo_upload_gs(struct st_context *st)
{
   static const int zero = 0;
   struct ureg_program *ureg;
   struct ureg_dst out_pos;
   struct ureg_dst out_layer;
   struct ureg_src in_pos;
   struct ureg_src imm;
   unsigned i;

   ureg = ureg_create(TGSI_PROCESSOR_GEOMETRY);
   if (!ureg)
      return NULL;

   ureg_property(ureg, TGSI_PROPERTY_GS_INPUT_PRIM, PIPE_PRIM_TRIANGLES);
   ureg_property(ureg, TGSI_PROPERTY_GS_OUTPUT_PRIM, PIPE_PRIM_TRIANGLE_STRIP);
   ureg_property(ureg, TGSI_PROPERTY_GS_MAX_OUTPUT_VERTICES, 3);

   out_pos = ureg_DECL_output(ureg, TGSI_SEMANTIC_POSITION, 0);
   out_layer = ureg_DECL_output(ureg, TGSI_SEMANTIC_LAYER, 0);

   in_pos = ureg_DECL_input(ureg, TGSI_SEMANTIC_POSITION, 0, 0, 1);

   imm = ureg_DECL_immediate_int(ureg, &zero, 1);

   for (i = 0; i < 3; ++i) {
      struct ureg_src in_pos_vertex = ureg_src_dimension(in_pos, i);

      /* out_pos = in_pos[i] */
      ureg_MOV(ureg, out_pos, in_pos_vertex);

      /* out_layer.x = f2i(in_pos[i].z) */
      ureg_F2I(ureg, ureg_writemask(out_layer, TGSI_WRITEMASK_X),
                     ureg_scalar(in_pos_vertex, TGSI_SWIZZLE_Z));

      ureg_EMIT(ureg, ureg_scalar(imm, TGSI_SWIZZLE_X));
   }

   ureg_END(ureg);

   return ureg_create_shader_and_destroy(ureg, st->pipe);
}

static void *
create_pbo_upload_fs(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct ureg_program *ureg;
   struct ureg_dst out;
   struct ureg_src sampler;
   struct ureg_src pos;
   struct ureg_src layer;
   struct ureg_src const0;
   struct ureg_dst temp0;

   ureg = ureg_create(TGSI_PROCESSOR_FRAGMENT);
   if (!ureg)
      return NULL;

   out     = ureg_DECL_output(ureg, TGSI_SEMANTIC_COLOR, 0);
   sampler = ureg_DECL_sampler(ureg, 0);
   if (screen->get_param(screen, PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL)) {
      pos = ureg_DECL_system_value(ureg, TGSI_SEMANTIC_POSITION, 0);
   } else {
      pos = ureg_DECL_fs_input(ureg, TGSI_SEMANTIC_POSITION, 0,
                               TGSI_INTERPOLATE_LINEAR);
   }
   if (st->pbo_upload.upload_layers) {
      layer = ureg_DECL_fs_input(ureg, TGSI_SEMANTIC_LAYER, 0,
                                       TGSI_INTERPOLATE_CONSTANT);
   }
   const0  = ureg_DECL_constant(ureg, 0);
   temp0   = ureg_DECL_temporary(ureg);

   /* Note: const0 = [ -xoffset + skip_pixels, -yoffset, stride, image_height ] */

   /* temp0.xy = f2i(temp0.xy) */
   ureg_F2I(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_XY),
                  ureg_swizzle(pos,
                               TGSI_SWIZZLE_X, TGSI_SWIZZLE_Y,
                               TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Y));

   /* temp0.xy = temp0.xy + const0.xy */
   ureg_UADD(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_XY),
                   ureg_swizzle(ureg_src(temp0),
                                TGSI_SWIZZLE_X, TGSI_SWIZZLE_Y,
                                TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Y),
                   ureg_swizzle(const0,
                                TGSI_SWIZZLE_X, TGSI_SWIZZLE_Y,
                                TGSI_SWIZZLE_Y, TGSI_SWIZZLE_Y));

   /* temp0.x = const0.z * temp0.y + temp0.x */
   ureg_UMAD(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_X),
                   ureg_scalar(const0, TGSI_SWIZZLE_Z),
                   ureg_scalar(ureg_src(temp0), TGSI_SWIZZLE_Y),
                   ureg_scalar(ureg_src(temp0), TGSI_SWIZZLE_X));

   if (st->pbo_upload.upload_layers) {
      /* temp0.x = const0.w * layer + temp0.x */
      ureg_UMAD(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_X),
                      ureg_scalar(const0, TGSI_SWIZZLE_W),
                      ureg_scalar(layer, TGSI_SWIZZLE_X),
                      ureg_scalar(ureg_src(temp0), TGSI_SWIZZLE_X));
   }

   /* temp0.w = 0 */
   ureg_MOV(ureg, ureg_writemask(temp0, TGSI_WRITEMASK_W), ureg_imm1u(ureg, 0));

   /* out = txf(sampler, temp0.x) */
   ureg_TXF(ureg, out, TGSI_TEXTURE_BUFFER, ureg_src(temp0), sampler);

   ureg_release_temporary(ureg, temp0);

   ureg_END(ureg);

   return ureg_create_shader_and_destroy(ureg, pipe);
}

static bool
try_pbo_upload_common(struct gl_context *ctx,
                      struct pipe_surface *surface,
                      int xoffset, int yoffset,
                      unsigned upload_width, unsigned upload_height,
                      struct pipe_resource *buffer,
                      enum pipe_format src_format,
                      intptr_t buf_offset,
                      unsigned bytes_per_pixel,
                      unsigned stride,
                      unsigned image_height)
{
   struct st_context *st = st_context(ctx);
   struct cso_context *cso = st->cso_context;
   struct pipe_context *pipe = st->pipe;
   unsigned depth = surface->u.tex.last_layer - surface->u.tex.first_layer + 1;
   unsigned skip_pixels = 0;
   bool success = false;

   /* Check alignment. */
   {
      unsigned ofs = (buf_offset * bytes_per_pixel) % ctx->Const.TextureBufferOffsetAlignment;
      if (ofs != 0) {
         if (ofs % bytes_per_pixel != 0)
            return false;

         skip_pixels = ofs / bytes_per_pixel;
         buf_offset -= skip_pixels;
      }
   }

   /* Create the shaders */
   if (!st->pbo_upload.vs) {
      st->pbo_upload.vs = create_pbo_upload_vs(st);
      if (!st->pbo_upload.vs)
         return false;
   }

   if (depth != 1 && st->pbo_upload.use_gs && !st->pbo_upload.gs) {
      st->pbo_upload.gs = create_pbo_upload_gs(st);
      if (!st->pbo_upload.gs)
         return false;
   }

   if (!st->pbo_upload.fs) {
      st->pbo_upload.fs = create_pbo_upload_fs(st);
      if (!st->pbo_upload.fs)
         return false;
   }

   cso_save_state(cso, (CSO_BIT_FRAGMENT_SAMPLER_VIEWS |
                        CSO_BIT_FRAGMENT_SAMPLERS |
                        CSO_BIT_VERTEX_ELEMENTS |
                        CSO_BIT_AUX_VERTEX_BUFFER_SLOT |
                        CSO_BIT_FRAMEBUFFER |
                        CSO_BIT_VIEWPORT |
                        CSO_BIT_BLEND |
                        CSO_BIT_DEPTH_STENCIL_ALPHA |
                        CSO_BIT_RASTERIZER |
                        CSO_BIT_STREAM_OUTPUTS |
                        CSO_BITS_ALL_SHADERS));
   cso_save_constant_buffer_slot0(cso, PIPE_SHADER_FRAGMENT);


   /* Set up the sampler_view */
   {
      unsigned first_element = buf_offset;
      unsigned last_element = buf_offset + skip_pixels + upload_width - 1
         + (upload_height - 1 + (depth - 1) * image_height) * stride;
      struct pipe_sampler_view templ;
      struct pipe_sampler_view *sampler_view;
      struct pipe_sampler_state sampler = {0};
      const struct pipe_sampler_state *samplers[1] = {&sampler};

      /* This should be ensured by Mesa before calling our callbacks */
      assert((last_element + 1) * bytes_per_pixel <= buffer->width0);

      if (last_element - first_element > ctx->Const.MaxTextureBufferSize - 1)
         goto fail;

      memset(&templ, 0, sizeof(templ));
      templ.target = PIPE_BUFFER;
      templ.format = src_format;
      templ.u.buf.first_element = first_element;
      templ.u.buf.last_element = last_element;
      templ.swizzle_r = PIPE_SWIZZLE_RED;
      templ.swizzle_g = PIPE_SWIZZLE_GREEN;
      templ.swizzle_b = PIPE_SWIZZLE_BLUE;
      templ.swizzle_a = PIPE_SWIZZLE_ALPHA;

      sampler_view = pipe->create_sampler_view(pipe, buffer, &templ);
      if (sampler_view == NULL)
         goto fail;

      cso_set_sampler_views(cso, PIPE_SHADER_FRAGMENT, 1, &sampler_view);

      pipe_sampler_view_reference(&sampler_view, NULL);

      cso_set_samplers(cso, PIPE_SHADER_FRAGMENT, 1, samplers);
   }

   /* Upload vertices */
   {
      struct pipe_vertex_buffer vbo;
      struct pipe_vertex_element velem;

      float x0 = (float) xoffset / surface->width * 2.0f - 1.0f;
      float y0 = (float) yoffset / surface->height * 2.0f - 1.0f;
      float x1 = (float) (xoffset + upload_width) / surface->width * 2.0f - 1.0f;
      float y1 = (float) (yoffset + upload_height) / surface->height * 2.0f - 1.0f;

      float *verts = NULL;

      vbo.user_buffer = NULL;
      vbo.buffer = NULL;
      vbo.stride = 2 * sizeof(float);

      u_upload_alloc(st->uploader, 0, 8 * sizeof(float), 4,
                     &vbo.buffer_offset, &vbo.buffer, (void **) &verts);
      if (!verts)
         goto fail;

      verts[0] = x0;
      verts[1] = y0;
      verts[2] = x0;
      verts[3] = y1;
      verts[4] = x1;
      verts[5] = y0;
      verts[6] = x1;
      verts[7] = y1;

      u_upload_unmap(st->uploader);

      velem.src_offset = 0;
      velem.instance_divisor = 0;
      velem.vertex_buffer_index = cso_get_aux_vertex_buffer_slot(cso);
      velem.src_format = PIPE_FORMAT_R32G32_FLOAT;

      cso_set_vertex_elements(cso, 1, &velem);

      cso_set_vertex_buffers(cso, velem.vertex_buffer_index, 1, &vbo);

      pipe_resource_reference(&vbo.buffer, NULL);
   }

   /* Upload constants */
   /* Note: the user buffer must be valid until draw time */
   struct {
      int32_t xoffset;
      int32_t yoffset;
      int32_t stride;
      int32_t image_size;
   } constants;

   {
      struct pipe_constant_buffer cb;

      constants.xoffset = -xoffset + skip_pixels;
      constants.yoffset = -yoffset;
      constants.stride = stride;
      constants.image_size = stride * image_height;

      if (st->constbuf_uploader) {
         cb.buffer = NULL;
         cb.user_buffer = NULL;
         u_upload_data(st->constbuf_uploader, 0, sizeof(constants),
                       ctx->Const.UniformBufferOffsetAlignment,
                       &constants, &cb.buffer_offset, &cb.buffer);
         if (!cb.buffer)
            goto fail;

         u_upload_unmap(st->constbuf_uploader);
      } else {
         cb.buffer = NULL;
         cb.user_buffer = &constants;
         cb.buffer_offset = 0;
      }
      cb.buffer_size = sizeof(constants);

      cso_set_constant_buffer(cso, PIPE_SHADER_FRAGMENT, 0, &cb);

      pipe_resource_reference(&cb.buffer, NULL);
   }

   /* Framebuffer_state */
   {
      struct pipe_framebuffer_state fb;
      memset(&fb, 0, sizeof(fb));
      fb.width = surface->width;
      fb.height = surface->height;
      fb.nr_cbufs = 1;
      pipe_surface_reference(&fb.cbufs[0], surface);

      cso_set_framebuffer(cso, &fb);

      pipe_surface_reference(&fb.cbufs[0], NULL);
   }

   cso_set_viewport_dims(cso, surface->width, surface->height, FALSE);

   /* Blend state */
   cso_set_blend(cso, &st->pbo_upload.blend);

   /* Depth/stencil/alpha state */
   {
      struct pipe_depth_stencil_alpha_state dsa;
      memset(&dsa, 0, sizeof(dsa));
      cso_set_depth_stencil_alpha(cso, &dsa);
   }

   /* Rasterizer state */
   cso_set_rasterizer(cso, &st->pbo_upload.raster);

   /* Set up the shaders */
   cso_set_vertex_shader_handle(cso, st->pbo_upload.vs);

   cso_set_geometry_shader_handle(cso, depth != 1 ? st->pbo_upload.gs : NULL);

   cso_set_tessctrl_shader_handle(cso, NULL);

   cso_set_tesseval_shader_handle(cso, NULL);

   cso_set_fragment_shader_handle(cso, st->pbo_upload.fs);

   /* Disable stream output */
   cso_set_stream_outputs(cso, 0, NULL, 0);

   if (depth == 1) {
      cso_draw_arrays(cso, PIPE_PRIM_TRIANGLE_STRIP, 0, 4);
   } else {
      cso_draw_arrays_instanced(cso, PIPE_PRIM_TRIANGLE_STRIP,
                                0, 4, 0, depth);
   }

   success = true;

fail:
   cso_restore_state(cso);
   cso_restore_constant_buffer_slot0(cso, PIPE_SHADER_FRAGMENT);

   return success;
}

static bool
try_pbo_upload(struct gl_context *ctx, GLuint dims,
               struct gl_texture_image *texImage,
               GLenum format, GLenum type,
               enum pipe_format dst_format,
               GLint xoffset, GLint yoffset, GLint zoffset,
               GLint width, GLint height, GLint depth,
               const void *pixels,
               const struct gl_pixelstore_attrib *unpack)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct st_texture_object *stObj = st_texture_object(texImage->TexObject);
   struct pipe_resource *texture = stImage->pt;
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct pipe_surface *surface = NULL;
   enum pipe_format src_format;
   const struct util_format_description *desc;
   GLenum gl_target = texImage->TexObject->Target;
   intptr_t buf_offset;
   unsigned bytes_per_pixel;
   unsigned stride, image_height;
   bool success;

   if (!st->pbo_upload.enabled)
      return false;

   /* From now on, we need the gallium representation of dimensions. */
   if (gl_target == GL_TEXTURE_1D_ARRAY) {
      depth = height;
      height = 1;
      zoffset = yoffset;
      yoffset = 0;
      image_height = 1;
   } else {
      image_height = unpack->ImageHeight > 0 ? unpack->ImageHeight : height;
   }

   if (depth != 1 && !st->pbo_upload.upload_layers)
      return false;

   /* Choose the source format. Initially, we do so without checking driver
    * support at all because of the remapping we later perform and because
    * at least the Radeon driver actually supports some formats for texture
    * buffers which it doesn't support for regular textures. */
   src_format = st_choose_matching_format(st, 0, format, type, unpack->SwapBytes);
   if (!src_format) {
      return false;
   }

   src_format = util_format_linear(src_format);
   desc = util_format_description(src_format);

   if (desc->layout != UTIL_FORMAT_LAYOUT_PLAIN)
      return false;

   if (desc->colorspace != UTIL_FORMAT_COLORSPACE_RGB)
      return false;

   if (st->pbo_upload.rgba_only) {
      enum pipe_format orig_dst_format = dst_format;

      if (!reinterpret_formats(&src_format, &dst_format)) {
         return false;
      }

      if (dst_format != orig_dst_format &&
          !screen->is_format_supported(screen, dst_format, PIPE_TEXTURE_2D, 0,
                                       PIPE_BIND_RENDER_TARGET)) {
         return false;
      }
   }

   if (!src_format ||
       !screen->is_format_supported(screen, src_format, PIPE_BUFFER, 0,
                                    PIPE_BIND_SAMPLER_VIEW)) {
      return false;
   }

   /* Check if the offset satisfies the alignment requirements */
   buf_offset = (intptr_t) pixels;
   bytes_per_pixel = desc->block.bits / 8;

   if (buf_offset % bytes_per_pixel) {
      return false;
   }

   /* Convert to texels */
   buf_offset = buf_offset / bytes_per_pixel;

   /* Compute the stride, taking unpack->Alignment into account */
   {
       unsigned pixels_per_row = unpack->RowLength > 0 ?
                           unpack->RowLength : width;
       unsigned bytes_per_row = pixels_per_row * bytes_per_pixel;
       unsigned remainder = bytes_per_row % unpack->Alignment;
       unsigned offset_rows;

       if (remainder > 0)
          bytes_per_row += (unpack->Alignment - remainder);

       if (bytes_per_row % bytes_per_pixel) {
          return false;
       }

       stride = bytes_per_row / bytes_per_pixel;

       offset_rows = unpack->SkipRows;
       if (dims == 3)
          offset_rows += image_height * unpack->SkipImages;

       buf_offset += unpack->SkipPixels + stride * offset_rows;
   }

   /* Set up the surface */
   {
      unsigned level = stObj->pt != stImage->pt ? 0 : texImage->TexObject->MinLevel + texImage->Level;
      unsigned max_layer = util_max_layer(texture, level);

      zoffset += texImage->Face + texImage->TexObject->MinLayer;

      struct pipe_surface templ;
      memset(&templ, 0, sizeof(templ));
      templ.format = dst_format;
      templ.u.tex.level = level;
      templ.u.tex.first_layer = MIN2(zoffset, max_layer);
      templ.u.tex.last_layer = MIN2(zoffset + depth - 1, max_layer);

      surface = pipe->create_surface(pipe, texture, &templ);
      if (!surface)
         return false;
   }

   success = try_pbo_upload_common(ctx,  surface,
                                   xoffset, yoffset, width, height,
                                   st_buffer_object(unpack->BufferObj)->buffer,
                                   src_format,
                                   buf_offset,
                                   bytes_per_pixel, stride, image_height);

   pipe_surface_reference(&surface, NULL);

   return success;
}

static void
st_TexSubImage(struct gl_context *ctx, GLuint dims,
               struct gl_texture_image *texImage,
               GLint xoffset, GLint yoffset, GLint zoffset,
               GLint width, GLint height, GLint depth,
               GLenum format, GLenum type, const void *pixels,
               const struct gl_pixelstore_attrib *unpack)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct st_texture_object *stObj = st_texture_object(texImage->TexObject);
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct pipe_resource *dst = stImage->pt;
   struct pipe_resource *src = NULL;
   struct pipe_resource src_templ;
   struct pipe_transfer *transfer;
   struct pipe_blit_info blit;
   enum pipe_format src_format, dst_format;
   mesa_format mesa_src_format;
   GLenum gl_target = texImage->TexObject->Target;
   unsigned bind;
   GLubyte *map;

   assert(!_mesa_is_format_etc2(texImage->TexFormat) &&
          texImage->TexFormat != MESA_FORMAT_ETC1_RGB8);

   if (!st->prefer_blit_based_texture_transfer) {
      goto fallback;
   }

   if (!dst) {
      goto fallback;
   }

   /* XXX Fallback for depth-stencil formats due to an incomplete stencil
    * blit implementation in some drivers. */
   if (format == GL_DEPTH_STENCIL) {
      goto fallback;
   }

   /* If the base internal format and the texture format don't match,
    * we can't use blit-based TexSubImage. */
   if (texImage->_BaseFormat !=
       _mesa_get_format_base_format(texImage->TexFormat)) {
      goto fallback;
   }


   /* See if the destination format is supported. */
   if (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL)
      bind = PIPE_BIND_DEPTH_STENCIL;
   else
      bind = PIPE_BIND_RENDER_TARGET;

   /* For luminance and intensity, only the red channel is stored
    * in the destination. */
   dst_format = util_format_linear(dst->format);
   dst_format = util_format_luminance_to_red(dst_format);
   dst_format = util_format_intensity_to_red(dst_format);

   if (!dst_format ||
       !screen->is_format_supported(screen, dst_format, dst->target,
                                    dst->nr_samples, bind)) {
      goto fallback;
   }

   if (_mesa_is_bufferobj(unpack->BufferObj)) {
      if (try_pbo_upload(ctx, dims, texImage, format, type, dst_format,
                         xoffset, yoffset, zoffset,
                         width, height, depth, pixels, unpack))
         return;
   }

   /* See if the texture format already matches the format and type,
    * in which case the memcpy-based fast path will likely be used and
    * we don't have to blit. */
   if (_mesa_format_matches_format_and_type(texImage->TexFormat, format,
                                            type, unpack->SwapBytes, NULL)) {
      goto fallback;
   }

   /* Choose the source format. */
   src_format = st_choose_matching_format(st, PIPE_BIND_SAMPLER_VIEW,
                                          format, type, unpack->SwapBytes);
   if (!src_format) {
      goto fallback;
   }

   mesa_src_format = st_pipe_format_to_mesa_format(src_format);

   /* There is no reason to do this if we cannot use memcpy for the temporary
    * source texture at least. This also takes transfer ops into account,
    * etc. */
   if (!_mesa_texstore_can_use_memcpy(ctx,
                             _mesa_get_format_base_format(mesa_src_format),
                             mesa_src_format, format, type, unpack)) {
      goto fallback;
   }

   /* TexSubImage only sets a single cubemap face. */
   if (gl_target == GL_TEXTURE_CUBE_MAP) {
      gl_target = GL_TEXTURE_2D;
   }
   /* TexSubImage can specify subsets of cube map array faces
    * so we need to upload via 2D array instead */
   if (gl_target == GL_TEXTURE_CUBE_MAP_ARRAY) {
      gl_target = GL_TEXTURE_2D_ARRAY;
   }

   /* Initialize the source texture description. */
   memset(&src_templ, 0, sizeof(src_templ));
   src_templ.target = gl_target_to_pipe(gl_target);
   src_templ.format = src_format;
   src_templ.bind = PIPE_BIND_SAMPLER_VIEW;
   src_templ.usage = PIPE_USAGE_STAGING;

   st_gl_texture_dims_to_pipe_dims(gl_target, width, height, depth,
                                   &src_templ.width0, &src_templ.height0,
                                   &src_templ.depth0, &src_templ.array_size);

   /* Check for NPOT texture support. */
   if (!screen->get_param(screen, PIPE_CAP_NPOT_TEXTURES) &&
       (!util_is_power_of_two(src_templ.width0) ||
        !util_is_power_of_two(src_templ.height0) ||
        !util_is_power_of_two(src_templ.depth0))) {
      goto fallback;
   }

   /* Create the source texture. */
   src = screen->resource_create(screen, &src_templ);
   if (!src) {
      goto fallback;
   }

   /* Map source pixels. */
   pixels = _mesa_validate_pbo_teximage(ctx, dims, width, height, depth,
                                        format, type, pixels, unpack,
                                        "glTexSubImage");
   if (!pixels) {
      /* This is a GL error. */
      pipe_resource_reference(&src, NULL);
      return;
   }

   /* From now on, we need the gallium representation of dimensions. */
   if (gl_target == GL_TEXTURE_1D_ARRAY) {
      zoffset = yoffset;
      yoffset = 0;
      depth = height;
      height = 1;
   }

   map = pipe_transfer_map_3d(pipe, src, 0, PIPE_TRANSFER_WRITE, 0, 0, 0,
                              width, height, depth, &transfer);
   if (!map) {
      _mesa_unmap_teximage_pbo(ctx, unpack);
      pipe_resource_reference(&src, NULL);
      goto fallback;
   }

   /* Upload pixels (just memcpy). */
   {
      const uint bytesPerRow = width * util_format_get_blocksize(src_format);
      GLuint row, slice;

      for (slice = 0; slice < (unsigned) depth; slice++) {
         if (gl_target == GL_TEXTURE_1D_ARRAY) {
            /* 1D array textures.
             * We need to convert gallium coords to GL coords.
             */
            GLvoid *src = _mesa_image_address2d(unpack, pixels,
                                                width, depth, format,
                                                type, slice, 0);
            memcpy(map, src, bytesPerRow);
         }
         else {
            ubyte *slice_map = map;

            for (row = 0; row < (unsigned) height; row++) {
               GLvoid *src = _mesa_image_address(dims, unpack, pixels,
                                                 width, height, format,
                                                 type, slice, row, 0);
               memcpy(slice_map, src, bytesPerRow);
               slice_map += transfer->stride;
            }
         }
         map += transfer->layer_stride;
      }
   }

   pipe_transfer_unmap(pipe, transfer);
   _mesa_unmap_teximage_pbo(ctx, unpack);

   /* Blit. */
   memset(&blit, 0, sizeof(blit));
   blit.src.resource = src;
   blit.src.level = 0;
   blit.src.format = src_format;
   blit.dst.resource = dst;
   blit.dst.level = stObj->pt != stImage->pt ? 0 : texImage->TexObject->MinLevel + texImage->Level;
   blit.dst.format = dst_format;
   blit.src.box.x = blit.src.box.y = blit.src.box.z = 0;
   blit.dst.box.x = xoffset;
   blit.dst.box.y = yoffset;
   blit.dst.box.z = zoffset + texImage->Face + texImage->TexObject->MinLayer;
   blit.src.box.width = blit.dst.box.width = width;
   blit.src.box.height = blit.dst.box.height = height;
   blit.src.box.depth = blit.dst.box.depth = depth;
   blit.mask = st_get_blit_mask(format, texImage->_BaseFormat);
   blit.filter = PIPE_TEX_FILTER_NEAREST;
   blit.scissor_enable = FALSE;

   st->pipe->blit(st->pipe, &blit);

   pipe_resource_reference(&src, NULL);
   return;

fallback:
   _mesa_store_texsubimage(ctx, dims, texImage, xoffset, yoffset, zoffset,
                           width, height, depth, format, type, pixels,
                           unpack);
}

static void
st_TexImage(struct gl_context * ctx, GLuint dims,
            struct gl_texture_image *texImage,
            GLenum format, GLenum type, const void *pixels,
            const struct gl_pixelstore_attrib *unpack)
{
   assert(dims == 1 || dims == 2 || dims == 3);

   prep_teximage(ctx, texImage, format, type);

   if (texImage->Width == 0 || texImage->Height == 0 || texImage->Depth == 0)
      return;

   /* allocate storage for texture data */
   if (!ctx->Driver.AllocTextureImageBuffer(ctx, texImage)) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage%uD", dims);
      return;
   }

   st_TexSubImage(ctx, dims, texImage, 0, 0, 0,
                  texImage->Width, texImage->Height, texImage->Depth,
                  format, type, pixels, unpack);
}


static void
st_CompressedTexSubImage(struct gl_context *ctx, GLuint dims,
                         struct gl_texture_image *texImage,
                         GLint x, GLint y, GLint z,
                         GLsizei w, GLsizei h, GLsizei d,
                         GLenum format, GLsizei imageSize, const GLvoid *data)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct st_texture_object *stObj = st_texture_object(texImage->TexObject);
   struct pipe_resource *texture = stImage->pt;
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct pipe_resource *dst = stImage->pt;
   struct pipe_surface *surface = NULL;
   struct compressed_pixelstore store;
   enum pipe_format copy_format;
   unsigned bytes_per_block;
   unsigned bw, bh;
   intptr_t buf_offset;
   bool success = false;

   /* Check basic pre-conditions for PBO upload */
   if (!st->prefer_blit_based_texture_transfer) {
      goto fallback;
   }

   if (!_mesa_is_bufferobj(ctx->Unpack.BufferObj))
      goto fallback;

   if ((_mesa_is_format_etc2(texImage->TexFormat) && !st->has_etc2) ||
       (texImage->TexFormat == MESA_FORMAT_ETC1_RGB8 && !st->has_etc1)) {
      /* ETC isn't supported and is represented by uncompressed formats. */
      goto fallback;
   }

   if (!dst) {
      goto fallback;
   }

   if (!st->pbo_upload.enabled ||
       !screen->get_param(screen, PIPE_CAP_SURFACE_REINTERPRET_BLOCKS)) {
      goto fallback;
   }

   /* Choose the pipe format for the upload. */
   bytes_per_block = util_format_get_blocksize(dst->format);
   bw = util_format_get_blockwidth(dst->format);
   bh = util_format_get_blockheight(dst->format);

   switch (bytes_per_block) {
   case 8:
      copy_format = PIPE_FORMAT_R16G16B16A16_UINT;
      break;
   case 16:
      copy_format = PIPE_FORMAT_R32G32B32A32_UINT;
      break;
   default:
      goto fallback;
   }

   if (!screen->is_format_supported(screen, copy_format, PIPE_BUFFER, 0,
                                    PIPE_BIND_SAMPLER_VIEW)) {
      goto fallback;
   }

   if (!screen->is_format_supported(screen, copy_format, dst->target,
                                    dst->nr_samples, PIPE_BIND_RENDER_TARGET)) {
      goto fallback;
   }

   /* Interpret the pixelstore settings. */
   _mesa_compute_compressed_pixelstore(dims, texImage->TexFormat, w, h, d,
                                       &ctx->Unpack, &store);
   assert(store.CopyBytesPerRow % bytes_per_block == 0);
   assert(store.SkipBytes % bytes_per_block == 0);

   /* Compute the offset into the buffer */
   buf_offset = (intptr_t)data + store.SkipBytes;

   if (buf_offset % bytes_per_block) {
      goto fallback;
   }

   buf_offset = buf_offset / bytes_per_block;

   /* Set up the surface. */
   {
      unsigned level = stObj->pt != stImage->pt ? 0 : texImage->TexObject->MinLevel + texImage->Level;
      unsigned max_layer = util_max_layer(texture, level);

      z += texImage->Face + texImage->TexObject->MinLayer;

      struct pipe_surface templ;
      memset(&templ, 0, sizeof(templ));
      templ.format = copy_format;
      templ.u.tex.level = level;
      templ.u.tex.first_layer = MIN2(z, max_layer);
      templ.u.tex.last_layer = MIN2(z + d - 1, max_layer);

      surface = pipe->create_surface(pipe, texture, &templ);
      if (!surface)
         goto fallback;
   }

   success = try_pbo_upload_common(ctx, surface,
                                   x / bw, y / bh,
                                   store.CopyBytesPerRow / bytes_per_block,
                                   store.CopyRowsPerSlice,
                                   st_buffer_object(ctx->Unpack.BufferObj)->buffer,
                                   copy_format,
                                   buf_offset,
                                   bytes_per_block,
                                   store.TotalBytesPerRow / bytes_per_block,
                                   store.TotalRowsPerSlice);

   pipe_surface_reference(&surface, NULL);

   if (success)
      return;

fallback:
   _mesa_store_compressed_texsubimage(ctx, dims, texImage,
                                      x, y, z, w, h, d,
                                      format, imageSize, data);
}

static void
st_CompressedTexImage(struct gl_context *ctx, GLuint dims,
                      struct gl_texture_image *texImage,
                      GLsizei imageSize, const GLvoid *data)
{
   prep_teximage(ctx, texImage, GL_NONE, GL_NONE);

   /* only 2D and 3D compressed images are supported at this time */
   if (dims == 1) {
      _mesa_problem(ctx, "Unexpected glCompressedTexImage1D call");
      return;
   }

   /* This is pretty simple, because unlike the general texstore path we don't
    * have to worry about the usual image unpacking or image transfer
    * operations.
    */
   assert(texImage);
   assert(texImage->Width > 0);
   assert(texImage->Height > 0);
   assert(texImage->Depth > 0);

   /* allocate storage for texture data */
   if (!st_AllocTextureImageBuffer(ctx, texImage)) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glCompressedTexImage%uD", dims);
      return;
   }

   st_CompressedTexSubImage(ctx, dims, texImage,
                            0, 0, 0,
                            texImage->Width, texImage->Height, texImage->Depth,
                            texImage->TexFormat,
                            imageSize, data);
}




/**
 * Called via ctx->Driver.GetTexSubImage()
 *
 * This uses a blit to copy the texture to a texture format which matches
 * the format and type combo and then a fast read-back is done using memcpy.
 * We can do arbitrary X/Y/Z/W/0/1 swizzling here as long as there is
 * a format which matches the swizzling.
 *
 * If such a format isn't available, it falls back to _mesa_GetTexImage_sw.
 *
 * NOTE: Drivers usually do a blit to convert between tiled and linear
 *       texture layouts during texture uploads/downloads, so the blit
 *       we do here should be free in such cases.
 */
static void
st_GetTexSubImage(struct gl_context * ctx,
                  GLint xoffset, GLint yoffset, GLint zoffset,
                  GLsizei width, GLsizei height, GLint depth,
                  GLenum format, GLenum type, GLvoid * pixels,
                  struct gl_texture_image *texImage)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct st_texture_object *stObj = st_texture_object(texImage->TexObject);
   struct pipe_resource *src = stObj->pt;
   struct pipe_resource *dst = NULL;
   struct pipe_resource dst_templ;
   enum pipe_format dst_format, src_format;
   mesa_format mesa_format;
   GLenum gl_target = texImage->TexObject->Target;
   enum pipe_texture_target pipe_target;
   struct pipe_blit_info blit;
   unsigned bind = PIPE_BIND_TRANSFER_READ;
   struct pipe_transfer *tex_xfer;
   ubyte *map = NULL;
   boolean done = FALSE;

   assert(!_mesa_is_format_etc2(texImage->TexFormat) &&
          texImage->TexFormat != MESA_FORMAT_ETC1_RGB8);

   if (!st->prefer_blit_based_texture_transfer &&
       !_mesa_is_format_compressed(texImage->TexFormat)) {
      /* Try to avoid the fallback if we're doing texture decompression here */
      goto fallback;
   }

   if (!stImage->pt || !src) {
      goto fallback;
   }

   /* XXX Fallback to _mesa_GetTexImage_sw for depth-stencil formats
    * due to an incomplete stencil blit implementation in some drivers. */
   if (format == GL_DEPTH_STENCIL || format == GL_STENCIL_INDEX) {
      goto fallback;
   }

   /* If the base internal format and the texture format don't match, we have
    * to fall back to _mesa_GetTexImage_sw. */
   if (texImage->_BaseFormat !=
       _mesa_get_format_base_format(texImage->TexFormat)) {
      goto fallback;
   }

   /* See if the texture format already matches the format and type,
    * in which case the memcpy-based fast path will be used. */
   if (_mesa_format_matches_format_and_type(texImage->TexFormat, format,
                                            type, ctx->Pack.SwapBytes, NULL)) {
      goto fallback;
   }

   /* Convert the source format to what is expected by GetTexImage
    * and see if it's supported.
    *
    * This only applies to glGetTexImage:
    * - Luminance must be returned as (L,0,0,1).
    * - Luminance alpha must be returned as (L,0,0,A).
    * - Intensity must be returned as (I,0,0,1)
    */
   if (stObj->surface_based)
      src_format = util_format_linear(stObj->surface_format);
   else
      src_format = util_format_linear(src->format);
   src_format = util_format_luminance_to_red(src_format);
   src_format = util_format_intensity_to_red(src_format);

   if (!src_format ||
       !screen->is_format_supported(screen, src_format, src->target,
                                    src->nr_samples,
                                    PIPE_BIND_SAMPLER_VIEW)) {
      goto fallback;
   }

   if (format == GL_DEPTH_COMPONENT || format == GL_DEPTH_STENCIL)
      bind |= PIPE_BIND_DEPTH_STENCIL;
   else
      bind |= PIPE_BIND_RENDER_TARGET;

   /* GetTexImage only returns a single face for cubemaps. */
   if (gl_target == GL_TEXTURE_CUBE_MAP) {
      gl_target = GL_TEXTURE_2D;
   }
   pipe_target = gl_target_to_pipe(gl_target);

   /* Choose the destination format by finding the best match
    * for the format+type combo. */
   dst_format = st_choose_matching_format(st, bind, format, type,
					  ctx->Pack.SwapBytes);

   if (dst_format == PIPE_FORMAT_NONE) {
      GLenum dst_glformat;

      /* Fall back to _mesa_GetTexImage_sw except for compressed formats,
       * where decompression with a blit is always preferred. */
      if (!util_format_is_compressed(src->format)) {
         goto fallback;
      }

      /* Set the appropriate format for the decompressed texture.
       * Luminance and sRGB formats shouldn't appear here.*/
      switch (src_format) {
      case PIPE_FORMAT_DXT1_RGB:
      case PIPE_FORMAT_DXT1_RGBA:
      case PIPE_FORMAT_DXT3_RGBA:
      case PIPE_FORMAT_DXT5_RGBA:
      case PIPE_FORMAT_RGTC1_UNORM:
      case PIPE_FORMAT_RGTC2_UNORM:
      case PIPE_FORMAT_ETC1_RGB8:
      case PIPE_FORMAT_BPTC_RGBA_UNORM:
         dst_glformat = GL_RGBA8;
         break;
      case PIPE_FORMAT_RGTC1_SNORM:
      case PIPE_FORMAT_RGTC2_SNORM:
         if (!ctx->Extensions.EXT_texture_snorm)
            goto fallback;
         dst_glformat = GL_RGBA8_SNORM;
         break;
      case PIPE_FORMAT_BPTC_RGB_FLOAT:
      case PIPE_FORMAT_BPTC_RGB_UFLOAT:
         if (!ctx->Extensions.ARB_texture_float)
            goto fallback;
         dst_glformat = GL_RGBA32F;
         break;
      default:
         assert(0);
         goto fallback;
      }

      dst_format = st_choose_format(st, dst_glformat, format, type,
                                    pipe_target, 0, bind, FALSE);

      if (dst_format == PIPE_FORMAT_NONE) {
         /* unable to get an rgba format!?! */
         goto fallback;
      }
   }

   /* create the destination texture of size (width X height X depth) */
   memset(&dst_templ, 0, sizeof(dst_templ));
   dst_templ.target = pipe_target;
   dst_templ.format = dst_format;
   dst_templ.bind = bind;
   dst_templ.usage = PIPE_USAGE_STAGING;

   st_gl_texture_dims_to_pipe_dims(gl_target, width, height, depth,
                                   &dst_templ.width0, &dst_templ.height0,
                                   &dst_templ.depth0, &dst_templ.array_size);

   dst = screen->resource_create(screen, &dst_templ);
   if (!dst) {
      goto fallback;
   }

   /* From now on, we need the gallium representation of dimensions. */
   if (gl_target == GL_TEXTURE_1D_ARRAY) {
      zoffset = yoffset;
      yoffset = 0;
      depth = height;
      height = 1;
   }

   assert(texImage->Face == 0 ||
          texImage->TexObject->MinLayer == 0 ||
          zoffset == 0);

   memset(&blit, 0, sizeof(blit));
   blit.src.resource = src;
   blit.src.level = texImage->Level + texImage->TexObject->MinLevel;
   blit.src.format = src_format;
   blit.dst.resource = dst;
   blit.dst.level = 0;
   blit.dst.format = dst->format;
   blit.src.box.x = xoffset;
   blit.dst.box.x = 0;
   blit.src.box.y = yoffset;
   blit.dst.box.y = 0;
   blit.src.box.z = texImage->Face + texImage->TexObject->MinLayer + zoffset;
   blit.dst.box.z = 0;
   blit.src.box.width = blit.dst.box.width = width;
   blit.src.box.height = blit.dst.box.height = height;
   blit.src.box.depth = blit.dst.box.depth = depth;
   blit.mask = st_get_blit_mask(texImage->_BaseFormat, format);
   blit.filter = PIPE_TEX_FILTER_NEAREST;
   blit.scissor_enable = FALSE;

   /* blit/render/decompress */
   st->pipe->blit(st->pipe, &blit);

   pixels = _mesa_map_pbo_dest(ctx, &ctx->Pack, pixels);

   map = pipe_transfer_map_3d(pipe, dst, 0, PIPE_TRANSFER_READ,
                              0, 0, 0, width, height, depth, &tex_xfer);
   if (!map) {
      goto end;
   }

   mesa_format = st_pipe_format_to_mesa_format(dst_format);

   /* copy/pack data into user buffer */
   if (_mesa_format_matches_format_and_type(mesa_format, format, type,
                                            ctx->Pack.SwapBytes, NULL)) {
      /* memcpy */
      const uint bytesPerRow = width * util_format_get_blocksize(dst_format);
      GLuint row, slice;

      for (slice = 0; slice < depth; slice++) {
         if (gl_target == GL_TEXTURE_1D_ARRAY) {
            /* 1D array textures.
             * We need to convert gallium coords to GL coords.
             */
            GLvoid *dest = _mesa_image_address3d(&ctx->Pack, pixels,
                                                 width, depth, format,
                                                 type, 0, slice, 0);
            memcpy(dest, map, bytesPerRow);
         }
         else {
            ubyte *slice_map = map;

            for (row = 0; row < height; row++) {
               GLvoid *dest = _mesa_image_address3d(&ctx->Pack, pixels,
                                                    width, height, format,
                                                    type, slice, row, 0);
               memcpy(dest, slice_map, bytesPerRow);
               slice_map += tex_xfer->stride;
            }
         }
         map += tex_xfer->layer_stride;
      }
   }
   else {
      /* format translation via floats */
      GLuint row, slice;
      GLfloat *rgba;
      uint32_t dstMesaFormat;
      int dstStride, srcStride;

      assert(util_format_is_compressed(src->format));

      rgba = malloc(width * 4 * sizeof(GLfloat));
      if (!rgba) {
         goto end;
      }

      if (ST_DEBUG & DEBUG_FALLBACK)
         debug_printf("%s: fallback format translation\n", __func__);

      dstMesaFormat = _mesa_format_from_format_and_type(format, type);
      dstStride = _mesa_image_row_stride(&ctx->Pack, width, format, type);
      srcStride = 4 * width * sizeof(GLfloat);
      for (slice = 0; slice < depth; slice++) {
         if (gl_target == GL_TEXTURE_1D_ARRAY) {
            /* 1D array textures.
             * We need to convert gallium coords to GL coords.
             */
            GLvoid *dest = _mesa_image_address3d(&ctx->Pack, pixels,
                                                 width, depth, format,
                                                 type, 0, slice, 0);

            /* get float[4] rgba row from surface */
            pipe_get_tile_rgba_format(tex_xfer, map, 0, 0, width, 1,
                                      dst_format, rgba);

            _mesa_format_convert(dest, dstMesaFormat, dstStride,
                                 rgba, RGBA32_FLOAT, srcStride,
                                 width, 1, NULL);
         }
         else {
            for (row = 0; row < height; row++) {
               GLvoid *dest = _mesa_image_address3d(&ctx->Pack, pixels,
                                                    width, height, format,
                                                    type, slice, row, 0);

               /* get float[4] rgba row from surface */
               pipe_get_tile_rgba_format(tex_xfer, map, 0, row, width, 1,
                                         dst_format, rgba);

               _mesa_format_convert(dest, dstMesaFormat, dstStride,
                                    rgba, RGBA32_FLOAT, srcStride,
                                    width, 1, NULL);
            }
         }
         map += tex_xfer->layer_stride;
      }

      free(rgba);
   }
   done = TRUE;

end:
   if (map)
      pipe_transfer_unmap(pipe, tex_xfer);

   _mesa_unmap_pbo_dest(ctx, &ctx->Pack);
   pipe_resource_reference(&dst, NULL);

fallback:
   if (!done) {
      _mesa_GetTexSubImage_sw(ctx, xoffset, yoffset, zoffset,
                              width, height, depth,
                              format, type, pixels, texImage);
   }
}


/**
 * Do a CopyTexSubImage operation using a read transfer from the source,
 * a write transfer to the destination and get_tile()/put_tile() to access
 * the pixels/texels.
 *
 * Note: srcY=0=TOP of renderbuffer
 */
static void
fallback_copy_texsubimage(struct gl_context *ctx,
                          struct st_renderbuffer *strb,
                          struct st_texture_image *stImage,
                          GLenum baseFormat,
                          GLint destX, GLint destY, GLint slice,
                          GLint srcX, GLint srcY,
                          GLsizei width, GLsizei height)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_transfer *src_trans;
   GLubyte *texDest;
   enum pipe_transfer_usage transfer_usage;
   void *map;
   unsigned dst_width = width;
   unsigned dst_height = height;
   unsigned dst_depth = 1;
   struct pipe_transfer *transfer;

   if (ST_DEBUG & DEBUG_FALLBACK)
      debug_printf("%s: fallback processing\n", __func__);

   if (st_fb_orientation(ctx->ReadBuffer) == Y_0_TOP) {
      srcY = strb->Base.Height - srcY - height;
   }

   map = pipe_transfer_map(pipe,
                           strb->texture,
                           strb->surface->u.tex.level,
                           strb->surface->u.tex.first_layer,
                           PIPE_TRANSFER_READ,
                           srcX, srcY,
                           width, height, &src_trans);

   if ((baseFormat == GL_DEPTH_COMPONENT ||
        baseFormat == GL_DEPTH_STENCIL) &&
       util_format_is_depth_and_stencil(stImage->pt->format))
      transfer_usage = PIPE_TRANSFER_READ_WRITE;
   else
      transfer_usage = PIPE_TRANSFER_WRITE;

   texDest = st_texture_image_map(st, stImage, transfer_usage,
                                  destX, destY, slice,
                                  dst_width, dst_height, dst_depth,
                                  &transfer);

   if (baseFormat == GL_DEPTH_COMPONENT ||
       baseFormat == GL_DEPTH_STENCIL) {
      const GLboolean scaleOrBias = (ctx->Pixel.DepthScale != 1.0F ||
                                     ctx->Pixel.DepthBias != 0.0F);
      GLint row, yStep;
      uint *data;

      /* determine bottom-to-top vs. top-to-bottom order for src buffer */
      if (st_fb_orientation(ctx->ReadBuffer) == Y_0_TOP) {
         srcY = height - 1;
         yStep = -1;
      }
      else {
         srcY = 0;
         yStep = 1;
      }

      data = malloc(width * sizeof(uint));

      if (data) {
         /* To avoid a large temp memory allocation, do copy row by row */
         for (row = 0; row < height; row++, srcY += yStep) {
            pipe_get_tile_z(src_trans, map, 0, srcY, width, 1, data);
            if (scaleOrBias) {
               _mesa_scale_and_bias_depth_uint(ctx, width, data);
            }

            if (stImage->pt->target == PIPE_TEXTURE_1D_ARRAY) {
               pipe_put_tile_z(transfer, texDest + row*transfer->layer_stride,
                               0, 0, width, 1, data);
            }
            else {
               pipe_put_tile_z(transfer, texDest, 0, row, width, 1, data);
            }
         }
      }
      else {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glCopyTexSubImage()");
      }

      free(data);
   }
   else {
      /* RGBA format */
      GLfloat *tempSrc =
         malloc(width * height * 4 * sizeof(GLfloat));

      if (tempSrc && texDest) {
         const GLint dims = 2;
         GLint dstRowStride;
         struct gl_texture_image *texImage = &stImage->base;
         struct gl_pixelstore_attrib unpack = ctx->DefaultPacking;

         if (st_fb_orientation(ctx->ReadBuffer) == Y_0_TOP) {
            unpack.Invert = GL_TRUE;
         }

         if (stImage->pt->target == PIPE_TEXTURE_1D_ARRAY) {
            dstRowStride = transfer->layer_stride;
         }
         else {
            dstRowStride = transfer->stride;
         }

         /* get float/RGBA image from framebuffer */
         /* XXX this usually involves a lot of int/float conversion.
          * try to avoid that someday.
          */
         pipe_get_tile_rgba_format(src_trans, map, 0, 0, width, height,
                                   util_format_linear(strb->texture->format),
                                   tempSrc);

         /* Store into texture memory.
          * Note that this does some special things such as pixel transfer
          * ops and format conversion.  In particular, if the dest tex format
          * is actually RGBA but the user created the texture as GL_RGB we
          * need to fill-in/override the alpha channel with 1.0.
          */
         _mesa_texstore(ctx, dims,
                        texImage->_BaseFormat, 
                        texImage->TexFormat, 
                        dstRowStride,
                        &texDest,
                        width, height, 1,
                        GL_RGBA, GL_FLOAT, tempSrc, /* src */
                        &unpack);
      }
      else {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexSubImage");
      }

      free(tempSrc);
   }

   st_texture_image_unmap(st, stImage, slice);
   pipe->transfer_unmap(pipe, src_trans);
}


/**
 * Do a CopyTex[Sub]Image1/2/3D() using a hardware (blit) path if possible.
 * Note that the region to copy has already been clipped so we know we
 * won't read from outside the source renderbuffer's bounds.
 *
 * Note: srcY=0=Bottom of renderbuffer (GL convention)
 */
static void
st_CopyTexSubImage(struct gl_context *ctx, GLuint dims,
                   struct gl_texture_image *texImage,
                   GLint destX, GLint destY, GLint slice,
                   struct gl_renderbuffer *rb,
                   GLint srcX, GLint srcY, GLsizei width, GLsizei height)
{
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct st_texture_object *stObj = st_texture_object(texImage->TexObject);
   struct st_renderbuffer *strb = st_renderbuffer(rb);
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct pipe_blit_info blit;
   enum pipe_format dst_format;
   GLboolean do_flip = (st_fb_orientation(ctx->ReadBuffer) == Y_0_TOP);
   unsigned bind;
   GLint srcY0, srcY1;

   assert(!_mesa_is_format_etc2(texImage->TexFormat) &&
          texImage->TexFormat != MESA_FORMAT_ETC1_RGB8);

   if (!strb || !strb->surface || !stImage->pt) {
      debug_printf("%s: null strb or stImage\n", __func__);
      return;
   }

   if (_mesa_texstore_needs_transfer_ops(ctx, texImage->_BaseFormat,
                                         texImage->TexFormat)) {
      goto fallback;
   }

   /* The base internal format must match the mesa format, so make sure
    * e.g. an RGB internal format is really allocated as RGB and not as RGBA.
    */
   if (texImage->_BaseFormat !=
       _mesa_get_format_base_format(texImage->TexFormat) ||
       rb->_BaseFormat != _mesa_get_format_base_format(rb->Format)) {
      goto fallback;
   }

   /* Choose the destination format to match the TexImage behavior. */
   dst_format = util_format_linear(stImage->pt->format);
   dst_format = util_format_luminance_to_red(dst_format);
   dst_format = util_format_intensity_to_red(dst_format);

   /* See if the destination format is supported. */
   if (texImage->_BaseFormat == GL_DEPTH_STENCIL ||
       texImage->_BaseFormat == GL_DEPTH_COMPONENT) {
      bind = PIPE_BIND_DEPTH_STENCIL;
   }
   else {
      bind = PIPE_BIND_RENDER_TARGET;
   }

   if (!dst_format ||
       !screen->is_format_supported(screen, dst_format, stImage->pt->target,
                                    stImage->pt->nr_samples, bind)) {
      goto fallback;
   }

   /* Y flipping for the main framebuffer. */
   if (do_flip) {
      srcY1 = strb->Base.Height - srcY - height;
      srcY0 = srcY1 + height;
   }
   else {
      srcY0 = srcY;
      srcY1 = srcY0 + height;
   }

   /* Blit the texture.
    * This supports flipping, format conversions, and downsampling.
    */
   memset(&blit, 0, sizeof(blit));
   blit.src.resource = strb->texture;
   blit.src.format = util_format_linear(strb->surface->format);
   blit.src.level = strb->surface->u.tex.level;
   blit.src.box.x = srcX;
   blit.src.box.y = srcY0;
   blit.src.box.z = strb->surface->u.tex.first_layer;
   blit.src.box.width = width;
   blit.src.box.height = srcY1 - srcY0;
   blit.src.box.depth = 1;
   blit.dst.resource = stImage->pt;
   blit.dst.format = dst_format;
   blit.dst.level = stObj->pt != stImage->pt ? 0 : texImage->Level + texImage->TexObject->MinLevel;
   blit.dst.box.x = destX;
   blit.dst.box.y = destY;
   blit.dst.box.z = stImage->base.Face + slice + texImage->TexObject->MinLayer;
   blit.dst.box.width = width;
   blit.dst.box.height = height;
   blit.dst.box.depth = 1;
   blit.mask = st_get_blit_mask(rb->_BaseFormat, texImage->_BaseFormat);
   blit.filter = PIPE_TEX_FILTER_NEAREST;
   pipe->blit(pipe, &blit);
   return;

fallback:
   /* software fallback */
   fallback_copy_texsubimage(ctx,
                             strb, stImage, texImage->_BaseFormat,
                             destX, destY, slice,
                             srcX, srcY, width, height);
}


/**
 * Copy image data from stImage into the texture object 'stObj' at level
 * 'dstLevel'.
 */
static void
copy_image_data_to_texture(struct st_context *st,
			   struct st_texture_object *stObj,
                           GLuint dstLevel,
			   struct st_texture_image *stImage)
{
   /* debug checks */
   {
      const struct gl_texture_image *dstImage =
         stObj->base.Image[stImage->base.Face][dstLevel];
      assert(dstImage);
      assert(dstImage->Width == stImage->base.Width);
      assert(dstImage->Height == stImage->base.Height);
      assert(dstImage->Depth == stImage->base.Depth);
   }

   if (stImage->pt) {
      /* Copy potentially with the blitter:
       */
      GLuint src_level;
      if (stImage->pt->last_level == 0)
         src_level = 0;
      else
         src_level = stImage->base.Level;

      assert(src_level <= stImage->pt->last_level);
      assert(u_minify(stImage->pt->width0, src_level) == stImage->base.Width);
      assert(stImage->pt->target == PIPE_TEXTURE_1D_ARRAY ||
             u_minify(stImage->pt->height0, src_level) == stImage->base.Height);
      assert(stImage->pt->target == PIPE_TEXTURE_2D_ARRAY ||
             stImage->pt->target == PIPE_TEXTURE_CUBE_ARRAY ||
             u_minify(stImage->pt->depth0, src_level) == stImage->base.Depth);

      st_texture_image_copy(st->pipe,
                            stObj->pt, dstLevel,  /* dest texture, level */
                            stImage->pt, src_level, /* src texture, level */
                            stImage->base.Face);

      pipe_resource_reference(&stImage->pt, NULL);
   }
   pipe_resource_reference(&stImage->pt, stObj->pt);
}


/**
 * Called during state validation.  When this function is finished,
 * the texture object should be ready for rendering.
 * \return GL_TRUE for success, GL_FALSE for failure (out of mem)
 */
GLboolean
st_finalize_texture(struct gl_context *ctx,
		    struct pipe_context *pipe,
		    struct gl_texture_object *tObj)
{
   struct st_context *st = st_context(ctx);
   struct st_texture_object *stObj = st_texture_object(tObj);
   const GLuint nr_faces = _mesa_num_tex_faces(stObj->base.Target);
   GLuint face;
   const struct st_texture_image *firstImage;
   enum pipe_format firstImageFormat;
   GLuint ptWidth, ptHeight, ptDepth, ptLayers, ptNumSamples;

   if (tObj->Immutable)
      return GL_TRUE;

   if (_mesa_is_texture_complete(tObj, &tObj->Sampler)) {
      /* The texture is complete and we know exactly how many mipmap levels
       * are present/needed.  This is conditional because we may be called
       * from the st_generate_mipmap() function when the texture object is
       * incomplete.  In that case, we'll have set stObj->lastLevel before
       * we get here.
       */
      if (stObj->base.Sampler.MinFilter == GL_LINEAR ||
          stObj->base.Sampler.MinFilter == GL_NEAREST)
         stObj->lastLevel = stObj->base.BaseLevel;
      else
         stObj->lastLevel = stObj->base._MaxLevel;
   }

   if (tObj->Target == GL_TEXTURE_BUFFER) {
      struct st_buffer_object *st_obj = st_buffer_object(tObj->BufferObject);

      if (!st_obj) {
         pipe_resource_reference(&stObj->pt, NULL);
         st_texture_release_all_sampler_views(st, stObj);
         return GL_TRUE;
      }

      if (st_obj->buffer != stObj->pt) {
         pipe_resource_reference(&stObj->pt, st_obj->buffer);
         st_texture_release_all_sampler_views(st, stObj);
         stObj->width0 = stObj->pt->width0 / _mesa_get_format_bytes(tObj->_BufferObjectFormat);
         stObj->height0 = 1;
         stObj->depth0 = 1;
      }
      return GL_TRUE;

   }

   firstImage = st_texture_image_const(_mesa_base_tex_image(&stObj->base));
   assert(firstImage);

   /* If both firstImage and stObj point to a texture which can contain
    * all active images, favour firstImage.  Note that because of the
    * completeness requirement, we know that the image dimensions
    * will match.
    */
   if (firstImage->pt &&
       firstImage->pt != stObj->pt &&
       (!stObj->pt || firstImage->pt->last_level >= stObj->pt->last_level)) {
      pipe_resource_reference(&stObj->pt, firstImage->pt);
      st_texture_release_all_sampler_views(st, stObj);
   }

   /* If this texture comes from a window system, there is nothing else to do. */
   if (stObj->surface_based) {
      return GL_TRUE;
   }

   /* Find gallium format for the Mesa texture */
   firstImageFormat =
      st_mesa_format_to_pipe_format(st, firstImage->base.TexFormat);

   /* Find size of level=0 Gallium mipmap image, plus number of texture layers */
   {
      GLuint width, height, depth;
      if (!guess_base_level_size(stObj->base.Target,
                                 firstImage->base.Width2,
                                 firstImage->base.Height2,
                                 firstImage->base.Depth2,
                                 firstImage->base.Level,
                                 &width, &height, &depth)) {
         width = stObj->width0;
         height = stObj->height0;
         depth = stObj->depth0;
      } else {
         /* The width/height/depth may have been previously reset in
          * guess_and_alloc_texture. */
         stObj->width0 = width;
         stObj->height0 = height;
         stObj->depth0 = depth;
      }
      /* convert GL dims to Gallium dims */
      st_gl_texture_dims_to_pipe_dims(stObj->base.Target, width, height, depth,
                                      &ptWidth, &ptHeight, &ptDepth, &ptLayers);
      ptNumSamples = firstImage->base.NumSamples;
   }

   /* If we already have a gallium texture, check that it matches the texture
    * object's format, target, size, num_levels, etc.
    */
   if (stObj->pt) {
      if (stObj->pt->target != gl_target_to_pipe(stObj->base.Target) ||
          stObj->pt->format != firstImageFormat ||
          stObj->pt->last_level < stObj->lastLevel ||
          stObj->pt->width0 != ptWidth ||
          stObj->pt->height0 != ptHeight ||
          stObj->pt->depth0 != ptDepth ||
          stObj->pt->nr_samples != ptNumSamples ||
          stObj->pt->array_size != ptLayers)
      {
         /* The gallium texture does not match the Mesa texture so delete the
          * gallium texture now.  We'll make a new one below.
          */
         pipe_resource_reference(&stObj->pt, NULL);
         st_texture_release_all_sampler_views(st, stObj);
         st->dirty.st |= ST_NEW_FRAMEBUFFER;
      }
   }

   /* May need to create a new gallium texture:
    */
   if (!stObj->pt) {
      GLuint bindings = default_bindings(st, firstImageFormat);

      stObj->pt = st_texture_create(st,
                                    gl_target_to_pipe(stObj->base.Target),
                                    firstImageFormat,
                                    stObj->lastLevel,
                                    ptWidth,
                                    ptHeight,
                                    ptDepth,
                                    ptLayers, ptNumSamples,
                                    bindings);

      if (!stObj->pt) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexImage");
         return GL_FALSE;
      }
   }

   /* Pull in any images not in the object's texture:
    */
   for (face = 0; face < nr_faces; face++) {
      GLuint level;
      for (level = stObj->base.BaseLevel; level <= stObj->lastLevel; level++) {
         struct st_texture_image *stImage =
            st_texture_image(stObj->base.Image[face][level]);

         /* Need to import images in main memory or held in other textures.
          */
         if (stImage && stObj->pt != stImage->pt) {
            if (level == 0 ||
                (stImage->base.Width == u_minify(stObj->width0, level) &&
                 stImage->base.Height == u_minify(stObj->height0, level) &&
                 stImage->base.Depth == u_minify(stObj->depth0, level))) {
               /* src image fits expected dest mipmap level size */
               copy_image_data_to_texture(st, stObj, level, stImage);
            }
         }
      }
   }

   return GL_TRUE;
}


/**
 * Called via ctx->Driver.AllocTextureStorage() to allocate texture memory
 * for a whole mipmap stack.
 */
static GLboolean
st_AllocTextureStorage(struct gl_context *ctx,
                       struct gl_texture_object *texObj,
                       GLsizei levels, GLsizei width,
                       GLsizei height, GLsizei depth)
{
   const GLuint numFaces = _mesa_num_tex_faces(texObj->Target);
   struct gl_texture_image *texImage = texObj->Image[0][0];
   struct st_context *st = st_context(ctx);
   struct st_texture_object *stObj = st_texture_object(texObj);
   struct pipe_screen *screen = st->pipe->screen;
   GLuint ptWidth, ptHeight, ptDepth, ptLayers, bindings;
   enum pipe_format fmt;
   GLint level;
   GLuint num_samples = texImage->NumSamples;

   assert(levels > 0);

   /* Save the level=0 dimensions */
   stObj->width0 = width;
   stObj->height0 = height;
   stObj->depth0 = depth;
   stObj->lastLevel = levels - 1;

   fmt = st_mesa_format_to_pipe_format(st, texImage->TexFormat);

   bindings = default_bindings(st, fmt);

   /* Raise the sample count if the requested one is unsupported. */
   if (num_samples > 1) {
      boolean found = FALSE;

      for (; num_samples <= ctx->Const.MaxSamples; num_samples++) {
         if (screen->is_format_supported(screen, fmt, PIPE_TEXTURE_2D,
                                         num_samples,
                                         PIPE_BIND_SAMPLER_VIEW)) {
            /* Update the sample count in gl_texture_image as well. */
            texImage->NumSamples = num_samples;
            found = TRUE;
            break;
         }
      }

      if (!found) {
         return GL_FALSE;
      }
   }

   st_gl_texture_dims_to_pipe_dims(texObj->Target,
                                   width, height, depth,
                                   &ptWidth, &ptHeight, &ptDepth, &ptLayers);

   stObj->pt = st_texture_create(st,
                                 gl_target_to_pipe(texObj->Target),
                                 fmt,
                                 levels - 1,
                                 ptWidth,
                                 ptHeight,
                                 ptDepth,
                                 ptLayers, num_samples,
                                 bindings);
   if (!stObj->pt)
      return GL_FALSE;

   /* Set image resource pointers */
   for (level = 0; level < levels; level++) {
      GLuint face;
      for (face = 0; face < numFaces; face++) {
         struct st_texture_image *stImage =
            st_texture_image(texObj->Image[face][level]);
         pipe_resource_reference(&stImage->pt, stObj->pt);
      }
   }

   return GL_TRUE;
}


static GLboolean
st_TestProxyTexImage(struct gl_context *ctx, GLenum target,
                     GLint level, mesa_format format,
                     GLint width, GLint height,
                     GLint depth, GLint border)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;

   if (width == 0 || height == 0 || depth == 0) {
      /* zero-sized images are legal, and always fit! */
      return GL_TRUE;
   }

   if (pipe->screen->can_create_resource) {
      /* Ask the gallium driver if the texture is too large */
      struct gl_texture_object *texObj =
         _mesa_get_current_tex_object(ctx, target);
      struct pipe_resource pt;

      /* Setup the pipe_resource object
       */
      memset(&pt, 0, sizeof(pt));

      pt.target = gl_target_to_pipe(target);
      pt.format = st_mesa_format_to_pipe_format(st, format);

      st_gl_texture_dims_to_pipe_dims(target,
                                      width, height, depth,
                                      &pt.width0, &pt.height0,
                                      &pt.depth0, &pt.array_size);

      if (level == 0 && (texObj->Sampler.MinFilter == GL_LINEAR ||
                         texObj->Sampler.MinFilter == GL_NEAREST)) {
         /* assume just one mipmap level */
         pt.last_level = 0;
      }
      else {
         /* assume a full set of mipmaps */
         pt.last_level = _mesa_logbase2(MAX3(width, height, depth));
      }

      return pipe->screen->can_create_resource(pipe->screen, &pt);
   }
   else {
      /* Use core Mesa fallback */
      return _mesa_test_proxy_teximage(ctx, target, level, format,
                                       width, height, depth, border);
   }
}

static GLboolean
st_TextureView(struct gl_context *ctx,
               struct gl_texture_object *texObj,
               struct gl_texture_object *origTexObj)
{
   struct st_texture_object *orig = st_texture_object(origTexObj);
   struct st_texture_object *tex = st_texture_object(texObj);
   struct gl_texture_image *image = texObj->Image[0][0];

   const int numFaces = _mesa_num_tex_faces(texObj->Target);
   const int numLevels = texObj->NumLevels;

   int face;
   int level;

   pipe_resource_reference(&tex->pt, orig->pt);

   /* Set image resource pointers */
   for (level = 0; level < numLevels; level++) {
      for (face = 0; face < numFaces; face++) {
         struct st_texture_image *stImage =
            st_texture_image(texObj->Image[face][level]);
         pipe_resource_reference(&stImage->pt, tex->pt);
      }
   }

   tex->surface_based = GL_TRUE;
   tex->surface_format =
      st_mesa_format_to_pipe_format(st_context(ctx), image->TexFormat);

   tex->width0 = image->Width;
   tex->height0 = image->Height;
   tex->depth0 = image->Depth;
   tex->lastLevel = numLevels - 1;

   return GL_TRUE;
}

static void
st_ClearTexSubImage(struct gl_context *ctx,
                    struct gl_texture_image *texImage,
                    GLint xoffset, GLint yoffset, GLint zoffset,
                    GLsizei width, GLsizei height, GLsizei depth,
                    const GLvoid *clearValue)
{
   static const char zeros[16] = {0};
   struct st_texture_image *stImage = st_texture_image(texImage);
   struct pipe_resource *pt = stImage->pt;
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   unsigned level = texImage->Level;
   struct pipe_box box;

   if (!pt)
      return;

   u_box_3d(xoffset, yoffset, zoffset + texImage->Face,
            width, height, depth, &box);
   if (texImage->TexObject->Immutable) {
      level += texImage->TexObject->MinLevel;
      box.z += texImage->TexObject->MinLayer;
   }

   pipe->clear_texture(pipe, pt, level, &box, clearValue ? clearValue : zeros);
}

void
st_init_texture_functions(struct dd_function_table *functions)
{
   functions->ChooseTextureFormat = st_ChooseTextureFormat;
   functions->QuerySamplesForFormat = st_QuerySamplesForFormat;
   functions->TexImage = st_TexImage;
   functions->TexSubImage = st_TexSubImage;
   functions->CompressedTexSubImage = st_CompressedTexSubImage;
   functions->CopyTexSubImage = st_CopyTexSubImage;
   functions->GenerateMipmap = st_generate_mipmap;

   functions->GetTexSubImage = st_GetTexSubImage;

   /* compressed texture functions */
   functions->CompressedTexImage = st_CompressedTexImage;
   functions->GetCompressedTexSubImage = _mesa_GetCompressedTexSubImage_sw;

   functions->NewTextureObject = st_NewTextureObject;
   functions->NewTextureImage = st_NewTextureImage;
   functions->DeleteTextureImage = st_DeleteTextureImage;
   functions->DeleteTexture = st_DeleteTextureObject;
   functions->AllocTextureImageBuffer = st_AllocTextureImageBuffer;
   functions->FreeTextureImageBuffer = st_FreeTextureImageBuffer;
   functions->MapTextureImage = st_MapTextureImage;
   functions->UnmapTextureImage = st_UnmapTextureImage;

   /* XXX Temporary until we can query pipe's texture sizes */
   functions->TestProxyTexImage = st_TestProxyTexImage;

   functions->AllocTextureStorage = st_AllocTextureStorage;
   functions->TextureView = st_TextureView;
   functions->ClearTexSubImage = st_ClearTexSubImage;
}
