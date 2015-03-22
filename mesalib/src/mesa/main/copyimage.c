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

static bool
prepare_target(struct gl_context *ctx, GLuint name, GLenum *target, int level,
               struct gl_texture_object **tex_obj,
               struct gl_texture_image **tex_image, GLuint *tmp_tex,
               const char *dbg_prefix)
{
   struct gl_renderbuffer *rb;

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
   switch (*target) {
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
                  _mesa_lookup_enum_by_nr(*target));
      return false;
   }

   if (*target == GL_RENDERBUFFER) {
      rb = _mesa_lookup_renderbuffer(ctx, name);
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

      if (rb->NumSamples > 1)
         *target = GL_TEXTURE_2D_MULTISAMPLE;
      else
         *target = GL_TEXTURE_2D;

      *tmp_tex = 0;
      _mesa_GenTextures(1, tmp_tex);
      if (*tmp_tex == 0)
         return false; /* Error already set by GenTextures */

      _mesa_BindTexture(*target, *tmp_tex);
      *tex_obj = _mesa_lookup_texture(ctx, *tmp_tex);
      *tex_image = _mesa_get_tex_image(ctx, *tex_obj, *target, 0);

      if (!ctx->Driver.BindRenderbufferTexImage(ctx, rb, *tex_image)) {
         _mesa_problem(ctx, "Failed to create texture from renderbuffer");
         return false;
      }

      if (ctx->Driver.FinishRenderTexture && !rb->NeedsFinishRenderTexture) {
         rb->NeedsFinishRenderTexture = true;
         ctx->Driver.FinishRenderTexture(ctx, rb);
      }
   } else {
      *tex_obj = _mesa_lookup_texture(ctx, name);
      if (!*tex_obj) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sName = %u)", dbg_prefix, name);
         return false;
      }

      _mesa_test_texobj_completeness(ctx, *tex_obj);
      if (!(*tex_obj)->_BaseComplete ||
          (level != 0 && !(*tex_obj)->_MipmapComplete)) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glCopyImageSubData(%sName incomplete)", dbg_prefix);
         return false;
      }

      if ((*tex_obj)->Target != *target) {
         _mesa_error(ctx, GL_INVALID_ENUM,
                     "glCopyImageSubData(%sTarget = %s)", dbg_prefix,
                     _mesa_lookup_enum_by_nr(*target));
         return false;
      }

      if (level < 0 || level >= MAX_TEXTURE_LEVELS) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sLevel = %d)", dbg_prefix, level);
         return false;
      }

      *tex_image = _mesa_select_tex_image(*tex_obj, *target, level);
      if (!*tex_image) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sLevel = %u)", dbg_prefix, level);
         return false;
      }
   }

   return true;
}

static bool
check_region_bounds(struct gl_context *ctx, struct gl_texture_image *tex_image,
                    int x, int y, int z, int width, int height, int depth,
                    const char *dbg_prefix)
{
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

   if (x + width > tex_image->Width) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(%sX or %sWidth exceeds image bounds)",
                  dbg_prefix, dbg_prefix);
      return false;
   }

   switch (tex_image->TexObject->Target) {
   case GL_TEXTURE_1D:
   case GL_TEXTURE_1D_ARRAY:
      if (y != 0 || height != 1) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sY or %sHeight exceeds image bounds)",
                     dbg_prefix, dbg_prefix);
         return false;
      }
      break;
   default:
      if (y + height > tex_image->Height) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sY or %sHeight exceeds image bounds)",
                     dbg_prefix, dbg_prefix);
         return false;
      }
      break;
   }

   switch (tex_image->TexObject->Target) {
   case GL_TEXTURE_1D:
   case GL_TEXTURE_2D:
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_RECTANGLE:
      if (z != 0 || depth != 1) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sZ or %sDepth exceeds image bounds)",
                     dbg_prefix, dbg_prefix);
         return false;
      }
      break;
   case GL_TEXTURE_CUBE_MAP:
      if (z < 0 || z + depth > 6) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sZ or %sDepth exceeds image bounds)",
                     dbg_prefix, dbg_prefix);
         return false;
      }
      break;
   case GL_TEXTURE_1D_ARRAY:
      if (z < 0 || z + depth > tex_image->Height) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sZ or %sDepth exceeds image bounds)",
                     dbg_prefix, dbg_prefix);
         return false;
      }
      break;
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
   case GL_TEXTURE_3D:
      if (z < 0 || z + depth > tex_image->Depth) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glCopyImageSubData(%sZ or %sDepth exceeds image bounds)",
                     dbg_prefix, dbg_prefix);
         return false;
      }
      break;
   }

   return true;
}

static bool
compressed_format_compatible(struct gl_context *ctx,
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
copy_format_compatible(struct gl_context *ctx,
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
   GLuint tmpTexNames[2] = { 0, 0 };
   struct gl_texture_object *srcTexObj, *dstTexObj;
   struct gl_texture_image *srcTexImage, *dstTexImage;
   GLuint src_bw, src_bh, dst_bw, dst_bh;
   int i, srcNewZ, dstNewZ;

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glCopyImageSubData(%u, %s, %d, %d, %d, %d, "
                                          "%u, %s, %d, %d, %d, %d, "
                                          "%d, %d, %d)\n",
                  srcName, _mesa_lookup_enum_by_nr(srcTarget), srcLevel,
                  srcX, srcY, srcZ,
                  dstName, _mesa_lookup_enum_by_nr(dstTarget), dstLevel,
                  dstX, dstY, dstZ,
                  srcWidth, srcHeight, srcWidth);

   if (!ctx->Extensions.ARB_copy_image) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glCopyImageSubData(extension not available)");
      return;
   }

   if (!prepare_target(ctx, srcName, &srcTarget, srcLevel,
                       &srcTexObj, &srcTexImage, &tmpTexNames[0], "src"))
      goto cleanup;

   if (!prepare_target(ctx, dstName, &dstTarget, dstLevel,
                       &dstTexObj, &dstTexImage, &tmpTexNames[1], "dst"))
      goto cleanup;

   _mesa_get_format_block_size(srcTexImage->TexFormat, &src_bw, &src_bh);
   if ((srcX % src_bw != 0) || (srcY % src_bh != 0) ||
       (srcWidth % src_bw != 0) || (srcHeight % src_bh != 0)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(unaligned src rectangle)");
      goto cleanup;
   }

   _mesa_get_format_block_size(dstTexImage->TexFormat, &dst_bw, &dst_bh);
   if ((dstX % dst_bw != 0) || (dstY % dst_bh != 0)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glCopyImageSubData(unaligned dst rectangle)");
      goto cleanup;
   }

   if (!check_region_bounds(ctx, srcTexImage, srcX, srcY, srcZ,
                            srcWidth, srcHeight, srcDepth, "src"))
      goto cleanup;

   if (!check_region_bounds(ctx, dstTexImage, dstX, dstY, dstZ,
                            (srcWidth / src_bw) * dst_bw,
                            (srcHeight / src_bh) * dst_bh, srcDepth, "dst"))
      goto cleanup;

   if (!copy_format_compatible(ctx, srcTexImage->InternalFormat,
                               dstTexImage->InternalFormat)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glCopyImageSubData(internalFormat mismatch)");
      goto cleanup;
   }

   for (i = 0; i < srcDepth; ++i) {
      if (srcTexObj->Target == GL_TEXTURE_CUBE_MAP) {
         srcTexImage = srcTexObj->Image[i + srcZ][srcLevel];
         srcNewZ = 0;
      } else {
         srcNewZ = srcZ + i;
      }

      if (dstTexObj->Target == GL_TEXTURE_CUBE_MAP) {
         dstTexImage = dstTexObj->Image[i + dstZ][dstLevel];
         dstNewZ = 0;
      } else {
         dstNewZ = dstZ + i;
      }

      ctx->Driver.CopyImageSubData(ctx, srcTexImage, srcX, srcY, srcNewZ,
                                   dstTexImage, dstX, dstY, dstNewZ,
                                   srcWidth, srcHeight);
   }

cleanup:
   _mesa_DeleteTextures(2, tmpTexNames);
}
