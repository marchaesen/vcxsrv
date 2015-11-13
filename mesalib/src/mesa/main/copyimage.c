/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2014 Intel Corporation.  All Rights Reserved.
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

#include "glheader.h"
#include "errors.h"
#include "enums.h"
#include "copyimage.h"
#include "teximage.h"
#include "texobj.h"
#include "fbobject.h"
#include "textureview.h"
#include "glformats.h"

enum mesa_block_class {
   BLOCK_CLASS_128_BITS,
   BLOCK_CLASS_64_BITS
};

/**
 * Prepare the source or destination resource.  This involves error
 * checking and returning the relevant gl_texture_image or gl_renderbuffer.
 * Note that one of the resulting tex_image or renderbuffer pointers will be
 * NULL and the other will be non-null.
 *
 * \param name  the texture or renderbuffer name
 * \param target  One of GL_TEXTURE_x target or GL_RENDERBUFFER
 * \param level  mipmap level
 * \param z  src or dest Z
 * \param depth  number of slices/faces/layers to copy
 * \param tex_image  returns a pointer to a texture image
 * \param renderbuffer  returns a pointer to a renderbuffer
 * \return true if success, false if error
 */
static bool
prepare_target(struct gl_context *ctx, GLuint name, GLenum target,
               int level, int z, int depth,
               struct gl_texture_image **tex_image,
               struct gl_renderbuffer **renderbuffer,
               mesa_format *format,
               GLenum *internalFormat,
               GLuint *width,
               GLuint *height,
               const char *dbg_prefix)
{
   if (name == 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sName = %d)", dbg_prefix, name);
      return false;
   }

   /*
    * INVALID_ENUM is generated
    *  * if either <srcTarget> or <dstTarget>
    *   - is not RENDERBUFFER or a valid non-proxy texture target
    *   - is TEXTURE_BUFFER, or
    *   - is one of the cubemap face selectors described in table 3.17,
    */
   switch (target) {
   case GL_RENDERBUFFER:
      /* Not a texture target, but valid */
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY:
   case GL_TEXTURE_2D:
   case GL_TEXTURE_3D:
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_RECTANGLE:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      /* These are all valid */
      break;
   case GL_TEXTURE_EXTERNAL_OES:
      /* Only exists in ES */
   case GL_TEXTURE_BUFFER:
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glCopyImageSubData(%sTarget = %s)", dbg_prefix,
                  _mesa_enum_to_string(target));
      return false;
   }

   if (target == GL_RENDERBUFFER) {
      struct gl_renderbuffer *rb = _mesa_lookup_renderbuffer(ctx, name);

      if (!rb) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sName = %u)", dbg_prefix, name);
         return false;
      }

      if (!rb->Name) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glCopyImageSubData(%sName incomplete)", dbg_prefix);
         return false;
      }

      if (level != 0) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sLevel = %u)", dbg_prefix, level);
         return false;
      }

      *renderbuffer = rb;
      *format = rb->Format;
      *internalFormat = rb->InternalFormat;
      *width = rb->Width;
      *height = rb->Height;
      *tex_image = NULL;
   } else {
      struct gl_texture_object *texObj = _mesa_lookup_texture(ctx, name);

      if (!texObj) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sName = %u)", dbg_prefix, name);
         return false;
      }

      _mesa_test_texobj_completeness(ctx, texObj);
      if (!texObj->_BaseComplete ||
          (level != 0 && !texObj->_MipmapComplete)) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glCopyImageSubData(%sName incomplete)", dbg_prefix);
         return false;
      }

      /* Note that target will not be a cube face name */
      if (texObj->Target != target) {
         /*
          * From GL_ARB_copy_image specification:
          * "INVALID_VALUE is generated if either <srcName> or <dstName> does
          * not correspond to a valid renderbuffer or texture object according
          * to the corresponding target parameter."
          */
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sTarget = %s)", dbg_prefix,
                     _mesa_enum_to_string(target));
         return false;
      }

      if (level < 0 || level >= MAX_TEXTURE_LEVELS) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sLevel = %d)", dbg_prefix, level);
         return false;
      }

      if (target == GL_TEXTURE_CUBE_MAP) {
         int i;

         assert(z < MAX_FACES);  /* should have been caught earlier */

         /* make sure all the cube faces are present */
         for (i = 0; i < depth; i++) {
            if (!texObj->Image[z+i][level]) {
               /* missing cube face */
               _mesa_error(ctx, GL_INVALID_OPERATION,
                           "glCopyImageSubData(missing cube face)");
               return false;
            }
         }

         *tex_image = texObj->Image[z][level];
      }
      else {
         *tex_image = _mesa_select_tex_image(texObj, target, level);
      }

      if (!*tex_image) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sLevel = %u)", dbg_prefix, level);
         return false;
      }

      *renderbuffer = NULL;
      *format = (*tex_image)->TexFormat;
      *internalFormat = (*tex_image)->InternalFormat;
      *width = (*tex_image)->Width;
      *height = (*tex_image)->Height;
   }

   return true;
}


/**
 * Check that the x,y,z,width,height,region is within the texture image
 * dimensions.
 * \return true if bounds OK, false if regions is out of bounds
 */
static bool
check_region_bounds(struct gl_context *ctx,
                    GLenum target,
                    const struct gl_texture_image *tex_image,
                    const struct gl_renderbuffer *renderbuffer,
                    int x, int y, int z, int width, int height, int depth,
                    const char *dbg_prefix)
{
   int surfWidth, surfHeight, surfDepth;

   if (width < 0 || height < 0 || depth < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sWidth, %sHeight, or %sDepth is negative)",
                  dbg_prefix, dbg_prefix, dbg_prefix);
      return false;
   }

   if (x < 0 || y < 0 || z < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sX, %sY, or %sZ is negative)",
                  dbg_prefix, dbg_prefix, dbg_prefix);
      return false;
   }

   /* Check X direction */
   if (target == GL_RENDERBUFFER) {
      surfWidth = renderbuffer->Width;
   }
   else {
      surfWidth = tex_image->Width;
   }

   if (x + width > surfWidth) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sX or %sWidth exceeds image bounds)",
                  dbg_prefix, dbg_prefix);
      return false;
   }

   /* Check Y direction */
   switch (target) {
   case GL_RENDERBUFFER:
      surfHeight = renderbuffer->Height;
      break;
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY:
      surfHeight = 1;
      break;
   default:
      surfHeight = tex_image->Height;
   }

   if (y + height > surfHeight) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sY or %sHeight exceeds image bounds)",
                  dbg_prefix, dbg_prefix);
      return false;
   }

   /* Check Z direction */
   switch (target) {
   case GL_RENDERBUFFER:
   case GL_TEXTURE_1D:
   case GL_TEXTURE_2D:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_RECTANGLE:
      surfDepth = 1;
      break;
   case GL_TEXTURE_CUBE_MAP:
      surfDepth = 6;
      break;
   case GL_TEXTURE_1D_ARRAY:
      surfDepth = tex_image->Height;
      break;
   default:
      surfDepth = tex_image->Depth;
   }

   if (z < 0 || z + depth > surfDepth) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sZ or %sDepth exceeds image bounds)",
                  dbg_prefix, dbg_prefix);
      return false;
   }

   return true;
}

static bool
compressed_format_compatible(const struct gl_context *ctx,
                             GLenum compressedFormat, GLenum otherFormat)
{
   enum mesa_block_class compressedClass, otherClass;

   /* Two view-incompatible compressed formats are never compatible. */
   if (_mesa_is_compressed_format(ctx, otherFormat)) {
      return false;
   }

   /*
    * From ARB_copy_image spec:
    *    Table 4.X.1 (Compatible internal formats for copying between
    *                 compressed and uncompressed internal formats)
    *    ---------------------------------------------------------------------
    *    | Texel / | Uncompressed      |                                     |
    *    | Block   | internal format   | Compressed internal format          |
    *    | size    |                   |                                     |
    *    ---------------------------------------------------------------------
    *    | 128-bit | RGBA32UI,         | COMPRESSED_RGBA_S3TC_DXT3_EXT,      |
    *    |         | RGBA32I,          | COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT,|
    *    |         | RGBA32F           | COMPRESSED_RGBA_S3TC_DXT5_EXT,      |
    *    |         |                   | COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT,|
    *    |         |                   | COMPRESSED_RG_RGTC2,                |
    *    |         |                   | COMPRESSED_SIGNED_RG_RGTC2,         |
    *    |         |                   | COMPRESSED_RGBA_BPTC_UNORM,         |
    *    |         |                   | COMPRESSED_SRGB_ALPHA_BPTC_UNORM,   |
    *    |         |                   | COMPRESSED_RGB_BPTC_SIGNED_FLOAT,   |
    *    |         |                   | COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT  |
    *    ---------------------------------------------------------------------
    *    | 64-bit  | RGBA16F, RG32F,   | COMPRESSED_RGB_S3TC_DXT1_EXT,       |
    *    |         | RGBA16UI, RG32UI, | COMPRESSED_SRGB_S3TC_DXT1_EXT,      |
    *    |         | RGBA16I, RG32I,   | COMPRESSED_RGBA_S3TC_DXT1_EXT,      |
    *    |         | RGBA16,           | COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT,|
    *    |         | RGBA16_SNORM      | COMPRESSED_RED_RGTC1,               |
    *    |         |                   | COMPRESSED_SIGNED_RED_RGTC1         |
    *    ---------------------------------------------------------------------
    */

   switch (compressedFormat) {
      case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
      case GL_COMPRESSED_RG_RGTC2:
      case GL_COMPRESSED_SIGNED_RG_RGTC2:
      case GL_COMPRESSED_RGBA_BPTC_UNORM:
      case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
      case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
      case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:
         compressedClass = BLOCK_CLASS_128_BITS;
         break;
      case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
      case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
      case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
      case GL_COMPRESSED_RED_RGTC1:
      case GL_COMPRESSED_SIGNED_RED_RGTC1:
         compressedClass = BLOCK_CLASS_64_BITS;
         break;
      default:
         return false;
   }

   switch (otherFormat) {
      case GL_RGBA32UI:
      case GL_RGBA32I:
      case GL_RGBA32F:
         otherClass = BLOCK_CLASS_128_BITS;
         break;
      case GL_RGBA16F:
      case GL_RG32F:
      case GL_RGBA16UI:
      case GL_RG32UI:
      case GL_RGBA16I:
      case GL_RG32I:
      case GL_RGBA16:
      case GL_RGBA16_SNORM:
         otherClass = BLOCK_CLASS_64_BITS;
         break;
      default:
         return false;
   }

   return compressedClass == otherClass;
}

static bool
copy_format_compatible(const struct gl_context *ctx,
                       GLenum srcFormat, GLenum dstFormat)
{
   /*
    * From ARB_copy_image spec:
    *    For the purposes of CopyImageSubData, two internal formats
    *    are considered compatible if any of the following conditions are
    *    met:
    *    * the formats are the same,
    *    * the formats are considered compatible according to the
    *      compatibility rules used for texture views as defined in
    *      section 3.9.X. In particular, if both internal formats are listed
    *      in the same entry of Table 3.X.2, they are considered compatible, or
    *    * one format is compressed and the other is uncompressed and
    *      Table 4.X.1 lists the two formats in the same row.
    */

   if (_mesa_texture_view_compatible_format(ctx, srcFormat, dstFormat)) {
      /* Also checks if formats are equal. */
      return true;
   } else if (_mesa_is_compressed_format(ctx, srcFormat)) {
      return compressed_format_compatible(ctx, srcFormat, dstFormat);
   } else if (_mesa_is_compressed_format(ctx, dstFormat)) {
      return compressed_format_compatible(ctx, dstFormat, srcFormat);
   }

   return false;
}

void GLAPIENTRY
_mesa_CopyImageSubData(GLuint srcName, GLenum srcTarget, GLint srcLevel,
                       GLint srcX, GLint srcY, GLint srcZ,
                       GLuint dstName, GLenum dstTarget, GLint dstLevel,
                       GLint dstX, GLint dstY, GLint dstZ,
                       GLsizei srcWidth, GLsizei srcHeight, GLsizei srcDepth)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_texture_image *srcTexImage, *dstTexImage;
   struct gl_renderbuffer *srcRenderbuffer, *dstRenderbuffer;
   mesa_format srcFormat, dstFormat;
   GLenum srcIntFormat, dstIntFormat;
   GLuint src_w, src_h, dst_w, dst_h;
   GLuint src_bw, src_bh, dst_bw, dst_bh;
   int dstWidth, dstHeight, dstDepth;
   int i;

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glCopyImageSubData(%u, %s, %d, %d, %d, %d, "
                                          "%u, %s, %d, %d, %d, %d, "
                                          "%d, %d, %d)\n",
                  srcName, _mesa_enum_to_string(srcTarget), srcLevel,
                  srcX, srcY, srcZ,
                  dstName, _mesa_enum_to_string(dstTarget), dstLevel,
                  dstX, dstY, dstZ,
                  srcWidth, srcHeight, srcDepth);

   if (!ctx->Extensions.ARB_copy_image) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glCopyImageSubData(extension not available)");
      return;
   }

   if (!prepare_target(ctx, srcName, srcTarget, srcLevel, srcZ, srcDepth,
                       &srcTexImage, &srcRenderbuffer, &srcFormat,
                       &srcIntFormat, &src_w, &src_h, "src"))
      return;

   if (!prepare_target(ctx, dstName, dstTarget, dstLevel, dstZ, srcDepth,
                       &dstTexImage, &dstRenderbuffer, &dstFormat,
                       &dstIntFormat, &dst_w, &dst_h, "dst"))
      return;

   _mesa_get_format_block_size(srcFormat, &src_bw, &src_bh);

   /* Section 18.3.2 (Copying Between Images) of the OpenGL 4.5 Core Profile
    * spec says:
    *
    *    An INVALID_VALUE error is generated if the dimensions of either
    *    subregion exceeds the boundaries of the corresponding image object,
    *    or if the image format is compressed and the dimensions of the
    *    subregion fail to meet the alignment constraints of the format.
    *
    * and Section 8.7 (Compressed Texture Images) says:
    *
    *    An INVALID_OPERATION error is generated if any of the following
    *    conditions occurs:
    *
    *      * width is not a multiple of four, and width + xoffset is not
    *        equal to the value of TEXTURE_WIDTH.
    *      * height is not a multiple of four, and height + yoffset is not
    *        equal to the value of TEXTURE_HEIGHT.
    *
    * so we take that to mean that you can copy the "last" block of a
    * compressed texture image even if it's smaller than the minimum block
    * dimensions.
    */
   if ((srcX % src_bw != 0) || (srcY % src_bh != 0) ||
       (srcWidth % src_bw != 0 && (srcX + srcWidth) != src_w) ||
       (srcHeight % src_bh != 0 && (srcY + srcHeight) != src_h)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(unaligned src rectangle)");
      return;
   }

   _mesa_get_format_block_size(dstFormat, &dst_bw, &dst_bh);
   if ((dstX % dst_bw != 0) || (dstY % dst_bh != 0)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(unaligned dst rectangle)");
      return;
   }

   /* From the GL_ARB_copy_image spec:
    *
    * "The dimensions are always specified in texels, even for compressed
    * texture formats. But it should be noted that if only one of the
    * source and destination textures is compressed then the number of
    * texels touched in the compressed image will be a factor of the
    * block size larger than in the uncompressed image."
    *
    * So, if copying from compressed to uncompressed, the dest region is
    * shrunk by the src block size factor.  If copying from uncompressed
    * to compressed, the dest region is grown by the dest block size factor.
    * Note that we're passed the _source_ width, height, depth and those
    * dimensions are never changed.
    */
   dstWidth = srcWidth * dst_bw / src_bw;
   dstHeight = srcHeight * dst_bh / src_bh;
   dstDepth = srcDepth;

   if (!check_region_bounds(ctx, srcTarget, srcTexImage, srcRenderbuffer,
                            srcX, srcY, srcZ, srcWidth, srcHeight, srcDepth,
                            "src"))
      return;

   if (!check_region_bounds(ctx, dstTarget, dstTexImage, dstRenderbuffer,
                            dstX, dstY, dstZ, dstWidth, dstHeight, dstDepth,
                            "dst"))
      return;

   if (!copy_format_compatible(ctx, srcIntFormat, dstIntFormat)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glCopyImageSubData(internalFormat mismatch)");
      return;
   }

   /* loop over 2D slices/faces/layers */
   for (i = 0; i < srcDepth; ++i) {
      int newSrcZ = srcZ + i;
      int newDstZ = dstZ + i;

      if (srcTexImage &&
          srcTexImage->TexObject->Target == GL_TEXTURE_CUBE_MAP) {
         /* need to update srcTexImage pointer for the cube face */
         assert(srcZ + i < MAX_FACES);
         srcTexImage = srcTexImage->TexObject->Image[srcZ + i][srcLevel];
         assert(srcTexImage);
         newSrcZ = 0;
      }

      if (dstTexImage &&
          dstTexImage->TexObject->Target == GL_TEXTURE_CUBE_MAP) {
         /* need to update dstTexImage pointer for the cube face */
         assert(dstZ + i < MAX_FACES);
         dstTexImage = dstTexImage->TexObject->Image[dstZ + i][dstLevel];
         assert(dstTexImage);
         newDstZ = 0;
      }

      ctx->Driver.CopyImageSubData(ctx,
                                   srcTexImage, srcRenderbuffer,
                                   srcX, srcY, newSrcZ,
                                   dstTexImage, dstRenderbuffer,
                                   dstX, dstY, newDstZ,
                                   srcWidth, srcHeight);
   }
}
