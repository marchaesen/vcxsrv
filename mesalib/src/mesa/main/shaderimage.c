/*
 * Copyright 2013 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Francisco Jerez <currojerez@riseup.net>
 */

#include <assert.h>

#include "shaderimage.h"
#include "mtypes.h"
#include "formats.h"
#include "errors.h"
#include "context.h"
#include "texobj.h"
#include "teximage.h"
#include "enums.h"

/*
 * Define endian-invariant aliases for some mesa formats that are
 * defined in terms of their channel layout from LSB to MSB in a
 * 32-bit word.  The actual byte offsets matter here because the user
 * is allowed to bit-cast one format into another and get predictable
 * results.
 */
#ifdef MESA_BIG_ENDIAN
# define MESA_FORMAT_RGBA_8 MESA_FORMAT_A8B8G8R8_UNORM
# define MESA_FORMAT_RG_16 MESA_FORMAT_G16R16_UNORM
# define MESA_FORMAT_RG_8 MESA_FORMAT_G8R8_UNORM
# define MESA_FORMAT_SIGNED_RGBA_8 MESA_FORMAT_A8B8G8R8_SNORM
# define MESA_FORMAT_SIGNED_RG_16 MESA_FORMAT_G16R16_SNORM
# define MESA_FORMAT_SIGNED_RG_8 MESA_FORMAT_G8R8_SNORM
#else
# define MESA_FORMAT_RGBA_8 MESA_FORMAT_R8G8B8A8_UNORM
# define MESA_FORMAT_RG_16 MESA_FORMAT_R16G16_UNORM
# define MESA_FORMAT_RG_8 MESA_FORMAT_R8G8_UNORM
# define MESA_FORMAT_SIGNED_RGBA_8 MESA_FORMAT_R8G8B8A8_SNORM
# define MESA_FORMAT_SIGNED_RG_16 MESA_FORMAT_R16G16_SNORM
# define MESA_FORMAT_SIGNED_RG_8 MESA_FORMAT_R8G8_SNORM
#endif

mesa_format
_mesa_get_shader_image_format(GLenum format)
{
   switch (format) {
   case GL_RGBA32F:
      return MESA_FORMAT_RGBA_FLOAT32;

   case GL_RGBA16F:
      return MESA_FORMAT_RGBA_FLOAT16;

   case GL_RG32F:
      return MESA_FORMAT_RG_FLOAT32;

   case GL_RG16F:
      return MESA_FORMAT_RG_FLOAT16;

   case GL_R11F_G11F_B10F:
      return MESA_FORMAT_R11G11B10_FLOAT;

   case GL_R32F:
      return MESA_FORMAT_R_FLOAT32;

   case GL_R16F:
      return MESA_FORMAT_R_FLOAT16;

   case GL_RGBA32UI:
      return MESA_FORMAT_RGBA_UINT32;

   case GL_RGBA16UI:
      return MESA_FORMAT_RGBA_UINT16;

   case GL_RGB10_A2UI:
      return MESA_FORMAT_R10G10B10A2_UINT;

   case GL_RGBA8UI:
      return MESA_FORMAT_RGBA_UINT8;

   case GL_RG32UI:
      return MESA_FORMAT_RG_UINT32;

   case GL_RG16UI:
      return MESA_FORMAT_RG_UINT16;

   case GL_RG8UI:
      return MESA_FORMAT_RG_UINT8;

   case GL_R32UI:
      return MESA_FORMAT_R_UINT32;

   case GL_R16UI:
      return MESA_FORMAT_R_UINT16;

   case GL_R8UI:
      return MESA_FORMAT_R_UINT8;

   case GL_RGBA32I:
      return MESA_FORMAT_RGBA_SINT32;

   case GL_RGBA16I:
      return MESA_FORMAT_RGBA_SINT16;

   case GL_RGBA8I:
      return MESA_FORMAT_RGBA_SINT8;

   case GL_RG32I:
      return MESA_FORMAT_RG_SINT32;

   case GL_RG16I:
      return MESA_FORMAT_RG_SINT16;

   case GL_RG8I:
      return MESA_FORMAT_RG_SINT8;

   case GL_R32I:
      return MESA_FORMAT_R_SINT32;

   case GL_R16I:
      return MESA_FORMAT_R_SINT16;

   case GL_R8I:
      return MESA_FORMAT_R_SINT8;

   case GL_RGBA16:
      return MESA_FORMAT_RGBA_UNORM16;

   case GL_RGB10_A2:
      return MESA_FORMAT_R10G10B10A2_UNORM;

   case GL_RGBA8:
      return MESA_FORMAT_RGBA_8;

   case GL_RG16:
      return MESA_FORMAT_RG_16;

   case GL_RG8:
      return MESA_FORMAT_RG_8;

   case GL_R16:
      return MESA_FORMAT_R_UNORM16;

   case GL_R8:
      return MESA_FORMAT_R_UNORM8;

   case GL_RGBA16_SNORM:
      return MESA_FORMAT_RGBA_SNORM16;

   case GL_RGBA8_SNORM:
      return MESA_FORMAT_SIGNED_RGBA_8;

   case GL_RG16_SNORM:
      return MESA_FORMAT_SIGNED_RG_16;

   case GL_RG8_SNORM:
      return MESA_FORMAT_SIGNED_RG_8;

   case GL_R16_SNORM:
      return MESA_FORMAT_R_SNORM16;

   case GL_R8_SNORM:
      return MESA_FORMAT_R_SNORM8;

   default:
      return MESA_FORMAT_NONE;
   }
}

enum image_format_class
{
   /** Not a valid image format. */
   IMAGE_FORMAT_CLASS_NONE = 0,

   /** Classes of image formats you can cast into each other. */
   /** \{ */
   IMAGE_FORMAT_CLASS_1X8,
   IMAGE_FORMAT_CLASS_1X16,
   IMAGE_FORMAT_CLASS_1X32,
   IMAGE_FORMAT_CLASS_2X8,
   IMAGE_FORMAT_CLASS_2X16,
   IMAGE_FORMAT_CLASS_2X32,
   IMAGE_FORMAT_CLASS_10_11_11,
   IMAGE_FORMAT_CLASS_4X8,
   IMAGE_FORMAT_CLASS_4X16,
   IMAGE_FORMAT_CLASS_4X32,
   IMAGE_FORMAT_CLASS_2_10_10_10
   /** \} */
};

static enum image_format_class
get_image_format_class(mesa_format format)
{
   switch (format) {
   case MESA_FORMAT_RGBA_FLOAT32:
      return IMAGE_FORMAT_CLASS_4X32;

   case MESA_FORMAT_RGBA_FLOAT16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_RG_FLOAT32:
      return IMAGE_FORMAT_CLASS_2X32;

   case MESA_FORMAT_RG_FLOAT16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_R11G11B10_FLOAT:
      return IMAGE_FORMAT_CLASS_10_11_11;

   case MESA_FORMAT_R_FLOAT32:
      return IMAGE_FORMAT_CLASS_1X32;

   case MESA_FORMAT_R_FLOAT16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_RGBA_UINT32:
      return IMAGE_FORMAT_CLASS_4X32;

   case MESA_FORMAT_RGBA_UINT16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_R10G10B10A2_UINT:
      return IMAGE_FORMAT_CLASS_2_10_10_10;

   case MESA_FORMAT_RGBA_UINT8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_RG_UINT32:
      return IMAGE_FORMAT_CLASS_2X32;

   case MESA_FORMAT_RG_UINT16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_RG_UINT8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_UINT32:
      return IMAGE_FORMAT_CLASS_1X32;

   case MESA_FORMAT_R_UINT16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_UINT8:
      return IMAGE_FORMAT_CLASS_1X8;

   case MESA_FORMAT_RGBA_SINT32:
      return IMAGE_FORMAT_CLASS_4X32;

   case MESA_FORMAT_RGBA_SINT16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_RGBA_SINT8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_RG_SINT32:
      return IMAGE_FORMAT_CLASS_2X32;

   case MESA_FORMAT_RG_SINT16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_RG_SINT8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_SINT32:
      return IMAGE_FORMAT_CLASS_1X32;

   case MESA_FORMAT_R_SINT16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_SINT8:
      return IMAGE_FORMAT_CLASS_1X8;

   case MESA_FORMAT_RGBA_UNORM16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_R10G10B10A2_UNORM:
      return IMAGE_FORMAT_CLASS_2_10_10_10;

   case MESA_FORMAT_RGBA_8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_RG_16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_RG_8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_UNORM16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_UNORM8:
      return IMAGE_FORMAT_CLASS_1X8;

   case MESA_FORMAT_RGBA_SNORM16:
      return IMAGE_FORMAT_CLASS_4X16;

   case MESA_FORMAT_SIGNED_RGBA_8:
      return IMAGE_FORMAT_CLASS_4X8;

   case MESA_FORMAT_SIGNED_RG_16:
      return IMAGE_FORMAT_CLASS_2X16;

   case MESA_FORMAT_SIGNED_RG_8:
      return IMAGE_FORMAT_CLASS_2X8;

   case MESA_FORMAT_R_SNORM16:
      return IMAGE_FORMAT_CLASS_1X16;

   case MESA_FORMAT_R_SNORM8:
      return IMAGE_FORMAT_CLASS_1X8;

   default:
      return IMAGE_FORMAT_CLASS_NONE;
   }
}

/**
 * Return whether an image format should be supported based on the current API
 * version of the context.
 */
static bool
is_image_format_supported(const struct gl_context *ctx, GLenum format)
{
   switch (format) {
   /* Formats supported on both desktop and ES GL, c.f. table 8.27 of the
    * OpenGL ES 3.1 specification.
    */
   case GL_RGBA32F:
   case GL_RGBA16F:
   case GL_R32F:
   case GL_RGBA32UI:
   case GL_RGBA16UI:
   case GL_RGBA8UI:
   case GL_R32UI:
   case GL_RGBA32I:
   case GL_RGBA16I:
   case GL_RGBA8I:
   case GL_R32I:
   case GL_RGBA8:
   case GL_RGBA8_SNORM:
      return true;

   /* Formats supported on unextended desktop GL and the original
    * ARB_shader_image_load_store extension, c.f. table 3.21 of the OpenGL 4.2
    * specification.
    */
   case GL_RG32F:
   case GL_RG16F:
   case GL_R11F_G11F_B10F:
   case GL_R16F:
   case GL_RGB10_A2UI:
   case GL_RG32UI:
   case GL_RG16UI:
   case GL_RG8UI:
   case GL_R16UI:
   case GL_R8UI:
   case GL_RG32I:
   case GL_RG16I:
   case GL_RG8I:
   case GL_R16I:
   case GL_R8I:
   case GL_RGBA16:
   case GL_RGB10_A2:
   case GL_RG16:
   case GL_RG8:
   case GL_R16:
   case GL_R8:
   case GL_RGBA16_SNORM:
   case GL_RG16_SNORM:
   case GL_RG8_SNORM:
   case GL_R16_SNORM:
   case GL_R8_SNORM:
      return _mesa_is_desktop_gl(ctx);

   default:
      return false;
   }
}

struct gl_image_unit
_mesa_default_image_unit(struct gl_context *ctx)
{
   const GLenum format = _mesa_is_desktop_gl(ctx) ? GL_R8 : GL_R32UI;
   const struct gl_image_unit u = {
      .Access = GL_READ_ONLY,
      .Format = format,
      ._ActualFormat = _mesa_get_shader_image_format(format)
   };
   return u;
}

void
_mesa_init_image_units(struct gl_context *ctx)
{
   unsigned i;

   for (i = 0; i < ARRAY_SIZE(ctx->ImageUnits); ++i)
      ctx->ImageUnits[i] = _mesa_default_image_unit(ctx);
}

GLboolean
_mesa_is_image_unit_valid(struct gl_context *ctx, struct gl_image_unit *u)
{
   struct gl_texture_object *t = u->TexObj;
   mesa_format tex_format;

   if (!t)
      return GL_FALSE;

   if (!t->_BaseComplete && !t->_MipmapComplete)
       _mesa_test_texobj_completeness(ctx, t);

   if (u->Level < t->BaseLevel ||
       u->Level > t->_MaxLevel ||
       (u->Level == t->BaseLevel && !t->_BaseComplete) ||
       (u->Level != t->BaseLevel && !t->_MipmapComplete))
      return GL_FALSE;

   if (_mesa_tex_target_is_layered(t->Target) &&
       u->_Layer >= _mesa_get_texture_layers(t, u->Level))
      return GL_FALSE;

   if (t->Target == GL_TEXTURE_BUFFER) {
      tex_format = _mesa_get_shader_image_format(t->BufferObjectFormat);

   } else {
      struct gl_texture_image *img = (t->Target == GL_TEXTURE_CUBE_MAP ?
                                      t->Image[u->_Layer][u->Level] :
                                      t->Image[0][u->Level]);

      if (!img || img->Border || img->NumSamples > ctx->Const.MaxImageSamples)
         return GL_FALSE;

      tex_format = _mesa_get_shader_image_format(img->InternalFormat);
   }

   if (!tex_format)
      return GL_FALSE;

   switch (t->ImageFormatCompatibilityType) {
   case GL_IMAGE_FORMAT_COMPATIBILITY_BY_SIZE:
      if (_mesa_get_format_bytes(tex_format) !=
          _mesa_get_format_bytes(u->_ActualFormat))
         return GL_FALSE;
      break;

   case GL_IMAGE_FORMAT_COMPATIBILITY_BY_CLASS:
      if (get_image_format_class(tex_format) !=
          get_image_format_class(u->_ActualFormat))
         return GL_FALSE;
      break;

   default:
      assert(!"Unexpected image format compatibility type");
   }

   return GL_TRUE;
}

static GLboolean
validate_bind_image_texture(struct gl_context *ctx, GLuint unit,
                            GLuint texture, GLint level, GLboolean layered,
                            GLint layer, GLenum access, GLenum format)
{
   assert(ctx->Const.MaxImageUnits <= MAX_IMAGE_UNITS);

   if (unit >= ctx->Const.MaxImageUnits) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindImageTexture(unit)");
      return GL_FALSE;
   }

   if (level < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindImageTexture(level)");
      return GL_FALSE;
   }

   if (layer < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindImageTexture(layer)");
      return GL_FALSE;
   }

   if (access != GL_READ_ONLY &&
       access != GL_WRITE_ONLY &&
       access != GL_READ_WRITE) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindImageTexture(access)");
      return GL_FALSE;
   }

   if (!is_image_format_supported(ctx, format)) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindImageTexture(format)");
      return GL_FALSE;
   }

   return GL_TRUE;
}

void GLAPIENTRY
_mesa_BindImageTexture(GLuint unit, GLuint texture, GLint level,
                       GLboolean layered, GLint layer, GLenum access,
                       GLenum format)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_image_unit *u;

   if (!validate_bind_image_texture(ctx, unit, texture, level,
                                    layered, layer, access, format))
      return;

   u = &ctx->ImageUnits[unit];

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewImageUnits;

   if (texture) {
      struct gl_texture_object *t = _mesa_lookup_texture(ctx, texture);

      if (!t) {
         _mesa_error(ctx, GL_INVALID_VALUE, "glBindImageTexture(texture)");
         return;
      }

      /* From section 8.22 "Texture Image Loads and Stores" of the OpenGL ES
       * 3.1 spec:
       *
       * "An INVALID_OPERATION error is generated if texture is not the name
       *  of an immutable texture object."
       */
      if (_mesa_is_gles(ctx) && !t->Immutable) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glBindImageTexture(!immutable)");
         return;
      }

      _mesa_reference_texobj(&u->TexObj, t);
   } else {
      _mesa_reference_texobj(&u->TexObj, NULL);
   }

   u->Level = level;
   u->Access = access;
   u->Format = format;
   u->_ActualFormat = _mesa_get_shader_image_format(format);

   if (u->TexObj && _mesa_tex_target_is_layered(u->TexObj->Target)) {
      u->Layered = layered;
      u->Layer = layer;
      u->_Layer = (u->Layered ? 0 : u->Layer);
   } else {
      u->Layered = GL_FALSE;
      u->Layer = 0;
   }
}

void GLAPIENTRY
_mesa_BindImageTextures(GLuint first, GLsizei count, const GLuint *textures)
{
   GET_CURRENT_CONTEXT(ctx);
   int i;

   if (!ctx->Extensions.ARB_shader_image_load_store) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glBindImageTextures()");
      return;
   }

   if (first + count > ctx->Const.MaxImageUnits) {
      /* The ARB_multi_bind spec says:
       *
       *    "An INVALID_OPERATION error is generated if <first> + <count>
       *     is greater than the number of image units supported by
       *     the implementation."
       */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindImageTextures(first=%u + count=%d > the value of "
                  "GL_MAX_IMAGE_UNITS=%u)",
                  first, count, ctx->Const.MaxImageUnits);
      return;
   }

   /* Assume that at least one binding will be changed */
   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewImageUnits;

   /* Note that the error semantics for multi-bind commands differ from
    * those of other GL commands.
    *
    * The Issues section in the ARB_multi_bind spec says:
    *
    *    "(11) Typically, OpenGL specifies that if an error is generated by
    *          a command, that command has no effect.  This is somewhat
    *          unfortunate for multi-bind commands, because it would require
    *          a first pass to scan the entire list of bound objects for
    *          errors and then a second pass to actually perform the
    *          bindings.  Should we have different error semantics?
    *
    *       RESOLVED:  Yes.  In this specification, when the parameters for
    *       one of the <count> binding points are invalid, that binding
    *       point is not updated and an error will be generated.  However,
    *       other binding points in the same command will be updated if
    *       their parameters are valid and no other error occurs."
    */

   _mesa_begin_texture_lookups(ctx);

   for (i = 0; i < count; i++) {
      struct gl_image_unit *u = &ctx->ImageUnits[first + i];
      const GLuint texture = textures ? textures[i] : 0;

      if (texture != 0) {
         struct gl_texture_object *texObj;
         GLenum tex_format;

         if (!u->TexObj || u->TexObj->Name != texture) {
            texObj = _mesa_lookup_texture_locked(ctx, texture);
            if (!texObj) {
               /* The ARB_multi_bind spec says:
                *
                *    "An INVALID_OPERATION error is generated if any value
                *     in <textures> is not zero or the name of an existing
                *     texture object (per binding)."
                */
               _mesa_error(ctx, GL_INVALID_OPERATION,
                           "glBindImageTextures(textures[%d]=%u "
                           "is not zero or the name of an existing texture "
                           "object)", i, texture);
               continue;
            }
         } else {
            texObj = u->TexObj;
         }

         if (texObj->Target == GL_TEXTURE_BUFFER) {
            tex_format = texObj->BufferObjectFormat;
         } else {
            struct gl_texture_image *image = texObj->Image[0][0];

            if (!image || image->Width == 0 || image->Height == 0 ||
                image->Depth == 0) {
               /* The ARB_multi_bind spec says:
                *
                *    "An INVALID_OPERATION error is generated if the width,
                *     height, or depth of the level zero texture image of
                *     any texture in <textures> is zero (per binding)."
                */
               _mesa_error(ctx, GL_INVALID_OPERATION,
                           "glBindImageTextures(the width, height or depth "
                           "of the level zero texture image of "
                           "textures[%d]=%u is zero)", i, texture);
               continue;
            }

            tex_format = image->InternalFormat;
         }

         if (!is_image_format_supported(ctx, tex_format)) {
            /* The ARB_multi_bind spec says:
             *
             *   "An INVALID_OPERATION error is generated if the internal
             *    format of the level zero texture image of any texture
             *    in <textures> is not found in table 8.33 (per binding)."
             */
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "glBindImageTextures(the internal format %s of "
                        "the level zero texture image of textures[%d]=%u "
                        "is not supported)",
                        _mesa_enum_to_string(tex_format),
                        i, texture);
            continue;
         }

         /* Update the texture binding */
         _mesa_reference_texobj(&u->TexObj, texObj);
         u->Level = 0;
         u->Layered = _mesa_tex_target_is_layered(texObj->Target);
         u->_Layer = u->Layer = 0;
         u->Access = GL_READ_WRITE;
         u->Format = tex_format;
         u->_ActualFormat = _mesa_get_shader_image_format(tex_format);
      } else {
         /* Unbind the texture from the unit */
         _mesa_reference_texobj(&u->TexObj, NULL);
         u->Level = 0;
         u->Layered = GL_FALSE;
         u->_Layer = u->Layer = 0;
         u->Access = GL_READ_ONLY;
         u->Format = GL_R8;
         u->_ActualFormat = MESA_FORMAT_R_UNORM8;
      }
   }

   _mesa_end_texture_lookups(ctx);
}

void GLAPIENTRY
_mesa_MemoryBarrier(GLbitfield barriers)
{
   GET_CURRENT_CONTEXT(ctx);

   if (ctx->Driver.MemoryBarrier)
      ctx->Driver.MemoryBarrier(ctx, barriers);
}

void GLAPIENTRY
_mesa_MemoryBarrierByRegion(GLbitfield barriers)
{
   GET_CURRENT_CONTEXT(ctx);

   GLbitfield all_allowed_bits = GL_ATOMIC_COUNTER_BARRIER_BIT |
                                 GL_FRAMEBUFFER_BARRIER_BIT |
                                 GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                                 GL_SHADER_STORAGE_BARRIER_BIT |
                                 GL_TEXTURE_FETCH_BARRIER_BIT |
                                 GL_UNIFORM_BARRIER_BIT;

   if (ctx->Driver.MemoryBarrier) {
      /* From section 7.11.2 of the OpenGL ES 3.1 specification:
       *
       *    "When barriers is ALL_BARRIER_BITS, shader memory accesses will be
       *     synchronized relative to all these barrier bits, but not to other
       *     barrier bits specific to MemoryBarrier."
       *
       * That is, if barriers is the special value GL_ALL_BARRIER_BITS, then all
       * barriers allowed by glMemoryBarrierByRegion should be activated."
       */
      if (barriers == GL_ALL_BARRIER_BITS) {
         ctx->Driver.MemoryBarrier(ctx, all_allowed_bits);
         return;
      }

      /* From section 7.11.2 of the OpenGL ES 3.1 specification:
       *
       *    "An INVALID_VALUE error is generated if barriers is not the special
       *     value ALL_BARRIER_BITS, and has any bits set other than those
       *     described above."
       */
      if ((barriers & ~all_allowed_bits) != 0) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glMemoryBarrierByRegion(unsupported barrier bit");
      }

      ctx->Driver.MemoryBarrier(ctx, barriers);
   }
}
