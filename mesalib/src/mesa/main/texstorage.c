/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2011  VMware, Inc.  All Rights Reserved.
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
 * \file texstorage.c
 * GL_ARB_texture_storage functions
 */

#include "glheader.h"
#include "context.h"
#include "enums.h"
#include "imports.h"
#include "macros.h"
#include "teximage.h"
#include "texobj.h"
#include "mipmap.h"
#include "texstorage.h"
#include "textureview.h"
#include "mtypes.h"
#include "glformats.h"
#include "hash.h"


/**
 * Check if the given texture target is a legal texture object target
 * for a glTexStorage() command.
 * This is a bit different than legal_teximage_target() when it comes
 * to cube maps.
 */
static GLboolean
legal_texobj_target(struct gl_context *ctx, GLuint dims, GLenum target)
{
   if (_mesa_is_gles3(ctx)
       && target != GL_TEXTURE_2D
       && target != GL_TEXTURE_CUBE_MAP
       && target != GL_TEXTURE_3D
       && target != GL_TEXTURE_2D_ARRAY)
      return GL_FALSE;

   switch (dims) {
   case 1:
      switch (target) {
      case GL_TEXTURE_1D:
      case GL_PROXY_TEXTURE_1D:
         return GL_TRUE;
      default:
         return GL_FALSE;
      }
   case 2:
      switch (target) {
      case GL_TEXTURE_2D:
      case GL_PROXY_TEXTURE_2D:
         return GL_TRUE;
      case GL_TEXTURE_CUBE_MAP:
      case GL_PROXY_TEXTURE_CUBE_MAP:
         return ctx->Extensions.ARB_texture_cube_map;
      case GL_TEXTURE_RECTANGLE:
      case GL_PROXY_TEXTURE_RECTANGLE:
         return ctx->Extensions.NV_texture_rectangle;
      case GL_TEXTURE_1D_ARRAY:
      case GL_PROXY_TEXTURE_1D_ARRAY:
         return ctx->Extensions.EXT_texture_array;
      default:
         return GL_FALSE;
      }
   case 3:
      switch (target) {
      case GL_TEXTURE_3D:
      case GL_PROXY_TEXTURE_3D:
         return GL_TRUE;
      case GL_TEXTURE_2D_ARRAY:
      case GL_PROXY_TEXTURE_2D_ARRAY:
         return ctx->Extensions.EXT_texture_array;
      case GL_TEXTURE_CUBE_MAP_ARRAY:
      case GL_PROXY_TEXTURE_CUBE_MAP_ARRAY:
         return ctx->Extensions.ARB_texture_cube_map_array;
      default:
         return GL_FALSE;
      }
   default:
      _mesa_problem(ctx, "invalid dims=%u in legal_texobj_target()", dims);
      return GL_FALSE;
   }
}


/** Helper to get a particular texture image in a texture object */
static struct gl_texture_image *
get_tex_image(struct gl_context *ctx,
              struct gl_texture_object *texObj,
              GLuint face, GLuint level)
{
   const GLenum faceTarget =
      (texObj->Target == GL_TEXTURE_CUBE_MAP ||
       texObj->Target == GL_PROXY_TEXTURE_CUBE_MAP)
      ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + face : texObj->Target;
   return _mesa_get_tex_image(ctx, texObj, faceTarget, level);
}



static GLboolean
initialize_texture_fields(struct gl_context *ctx,
                          struct gl_texture_object *texObj,
                          GLint levels,
                          GLsizei width, GLsizei height, GLsizei depth,
                          GLenum internalFormat, mesa_format texFormat)
{
   const GLenum target = texObj->Target;
   const GLuint numFaces = _mesa_num_tex_faces(target);
   GLint level, levelWidth = width, levelHeight = height, levelDepth = depth;
   GLuint face;

   /* Set up all the texture object's gl_texture_images */
   for (level = 0; level < levels; level++) {
      for (face = 0; face < numFaces; face++) {
         struct gl_texture_image *texImage =
            get_tex_image(ctx, texObj, face, level);

	 if (!texImage) {
	    _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexStorage");
            return GL_FALSE;
	 }

         _mesa_init_teximage_fields(ctx, texImage,
                                    levelWidth, levelHeight, levelDepth,
                                    0, internalFormat, texFormat);
      }

      _mesa_next_mipmap_level_size(target, 0,
                                   levelWidth, levelHeight, levelDepth,
                                   &levelWidth, &levelHeight, &levelDepth);
   }
   return GL_TRUE;
}


/**
 * Clear all fields of texture object to zeros.  Used for proxy texture tests
 * and to clean up when a texture memory allocation fails.
 */
static void
clear_texture_fields(struct gl_context *ctx,
                     struct gl_texture_object *texObj)
{
   const GLenum target = texObj->Target;
   const GLuint numFaces = _mesa_num_tex_faces(target);
   GLint level;
   GLuint face;

   for (level = 0; level < ARRAY_SIZE(texObj->Image[0]); level++) {
      for (face = 0; face < numFaces; face++) {
         struct gl_texture_image *texImage =
            get_tex_image(ctx, texObj, face, level);

	 if (!texImage) {
	    _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTexStorage");
            return;
	 }

         _mesa_init_teximage_fields(ctx, texImage,
                                    0, 0, 0, 0, /* w, h, d, border */
                                    GL_NONE, MESA_FORMAT_NONE);
      }
   }
}


/**
 * Update/re-validate framebuffer object.
 */
static void
update_fbo_texture(struct gl_context *ctx, struct gl_texture_object *texObj)
{
   const unsigned numFaces = _mesa_num_tex_faces(texObj->Target);
   for (int level = 0; level < ARRAY_SIZE(texObj->Image[0]); level++) {
      for (unsigned face = 0; face < numFaces; face++)
         _mesa_update_fbo_texture(ctx, texObj, face, level);
   }
}


GLboolean
_mesa_is_legal_tex_storage_format(const struct gl_context *ctx,
                                  GLenum internalformat)
{
   /* check internal format - note that only sized formats are allowed */
   switch (internalformat) {
   case GL_ALPHA:
   case GL_LUMINANCE:
   case GL_LUMINANCE_ALPHA:
   case GL_INTENSITY:
   case GL_RED:
   case GL_RG:
   case GL_RGB:
   case GL_RGBA:
   case GL_BGRA:
   case GL_DEPTH_COMPONENT:
   case GL_DEPTH_STENCIL:
   case GL_COMPRESSED_ALPHA:
   case GL_COMPRESSED_LUMINANCE_ALPHA:
   case GL_COMPRESSED_LUMINANCE:
   case GL_COMPRESSED_INTENSITY:
   case GL_COMPRESSED_RGB:
   case GL_COMPRESSED_RGBA:
   case GL_COMPRESSED_SRGB:
   case GL_COMPRESSED_SRGB_ALPHA:
   case GL_COMPRESSED_SLUMINANCE:
   case GL_COMPRESSED_SLUMINANCE_ALPHA:
   case GL_RED_INTEGER:
   case GL_GREEN_INTEGER:
   case GL_BLUE_INTEGER:
   case GL_ALPHA_INTEGER:
   case GL_RGB_INTEGER:
   case GL_RGBA_INTEGER:
   case GL_BGR_INTEGER:
   case GL_BGRA_INTEGER:
   case GL_LUMINANCE_INTEGER_EXT:
   case GL_LUMINANCE_ALPHA_INTEGER_EXT:
      /* these unsized formats are illegal */
      return GL_FALSE;
   default:
      return _mesa_base_tex_format(ctx, internalformat) > 0;
   }
}


/**
 * Default ctx->Driver.AllocTextureStorage() handler.
 *
 * The driver can override this with a more specific implementation if it
 * desires, but this can be used to get the texture images allocated using the
 * usual texture image handling code.  The immutability of
 * GL_ARB_texture_storage texture layouts is handled by texObj->Immutable
 * checks at glTexImage* time.
 */
GLboolean
_mesa_AllocTextureStorage_sw(struct gl_context *ctx,
                             struct gl_texture_object *texObj,
                             GLsizei levels, GLsizei width,
                             GLsizei height, GLsizei depth)
{
   const int numFaces = _mesa_num_tex_faces(texObj->Target);
   int face;
   int level;

   (void) width;
   (void) height;
   (void) depth;

   for (face = 0; face < numFaces; face++) {
      for (level = 0; level < levels; level++) {
         struct gl_texture_image *const texImage = texObj->Image[face][level];
         if (!ctx->Driver.AllocTextureImageBuffer(ctx, texImage))
            return GL_FALSE;
      }
   }

   return GL_TRUE;
}


/**
 * Do error checking for calls to glTexStorage1/2/3D().
 * If an error is found, record it with _mesa_error(), unless the target
 * is a proxy texture.
 * \return GL_TRUE if any error, GL_FALSE otherwise.
 */
static GLboolean
tex_storage_error_check(struct gl_context *ctx,
                        struct gl_texture_object *texObj,
                        GLuint dims, GLenum target,
                        GLsizei levels, GLenum internalformat,
                        GLsizei width, GLsizei height, GLsizei depth,
                        bool dsa)
{
   const char* suffix = dsa ? "ture" : "";

   /* Legal format checking has been moved to texstorage and texturestorage in
    * order to allow meta functions to use legacy formats. */

   /* size check */
   if (!_mesa_valid_tex_storage_dim(width, height, depth)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glTex%sStorage%uD(width, height or depth < 1)",
                  suffix, dims);
      return GL_TRUE;
   }

   if (_mesa_is_compressed_format(ctx, internalformat)) {
      GLenum err;
      if (!_mesa_target_can_be_compressed(ctx, target, internalformat, &err)) {
         _mesa_error(ctx, err,
                  "glTex%sStorage%dD(internalformat = %s)", suffix, dims,
                  _mesa_enum_to_string(internalformat));
         return GL_TRUE;
      }
   }

   /* levels check */
   if (levels < 1) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glTex%sStorage%uD(levels < 1)",
                  suffix, dims);
      return GL_TRUE;
   }

   /* check levels against maximum (note different error than above) */
   if (levels > (GLint) _mesa_max_texture_levels(ctx, target)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glTex%sStorage%uD(levels too large)",
                  suffix, dims);
      return GL_TRUE;
   }

   /* check levels against width/height/depth */
   if (levels > _mesa_get_tex_max_num_levels(target, width, height, depth)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glTex%sStorage%uD(too many levels"
                  " for max texture dimension)",
                  suffix, dims);
      return GL_TRUE;
   }

   /* non-default texture object check */
   if (!_mesa_is_proxy_texture(target) && (!texObj || (texObj->Name == 0))) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glTex%sStorage%uD(texture object 0)",
                  suffix, dims);
      return GL_TRUE;
   }

   /* Check if texObj->Immutable is set */
   if (!_mesa_is_proxy_texture(target) && texObj->Immutable) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glTex%sStorage%uD(immutable)",
                  suffix, dims);
      return GL_TRUE;
   }

   /* additional checks for depth textures */
   if (!_mesa_legal_texture_base_format_for_target(ctx, target, internalformat,
                                                   dims, dsa ?
                                                   "glTextureStorage" :
                                                   "glTexStorage"))
      return GL_TRUE;

   return GL_FALSE;
}


/**
 * Helper that does the storage allocation for _mesa_TexStorage1/2/3D()
 * and _mesa_TextureStorage1/2/3D().
 */
void
_mesa_texture_storage(struct gl_context *ctx, GLuint dims,
                      struct gl_texture_object *texObj,
                      GLenum target, GLsizei levels,
                      GLenum internalformat, GLsizei width,
                      GLsizei height, GLsizei depth, bool dsa)
{
   GLboolean sizeOK, dimensionsOK;
   mesa_format texFormat;
   const char* suffix = dsa ? "ture" : "";

   assert(texObj);

   if (tex_storage_error_check(ctx, texObj, dims, target, levels,
                               internalformat, width, height, depth, dsa)) {
      return; /* error was recorded */
   }

   texFormat = _mesa_choose_texture_format(ctx, texObj, target, 0,
                                           internalformat, GL_NONE, GL_NONE);
   assert(texFormat != MESA_FORMAT_NONE);

   /* check that width, height, depth are legal for the mipmap level */
   dimensionsOK = _mesa_legal_texture_dimensions(ctx, target, 0,
                                                  width, height, depth, 0);

   sizeOK = ctx->Driver.TestProxyTexImage(ctx, target, 0, texFormat,
                                          width, height, depth, 0);

   if (_mesa_is_proxy_texture(target)) {
      if (dimensionsOK && sizeOK) {
         initialize_texture_fields(ctx, texObj, levels, width, height, depth,
                                   internalformat, texFormat);
      }
      else {
         /* clear all image fields for [levels] */
         clear_texture_fields(ctx, texObj);
      }
   }
   else {
      if (!dimensionsOK) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glTex%sStorage%uD(invalid width, height or depth)",
                     suffix, dims);
         return;
      }

      if (!sizeOK) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY,
                     "glTex%sStorage%uD(texture too large)",
                     suffix, dims);
      }

      assert(levels > 0);
      assert(width > 0);
      assert(height > 0);
      assert(depth > 0);

      if (!initialize_texture_fields(ctx, texObj, levels, width, height, depth,
                                     internalformat, texFormat)) {
         return;
      }

      /* Do actual texture memory allocation */
      if (!ctx->Driver.AllocTextureStorage(ctx, texObj, levels,
                                           width, height, depth)) {
         /* Reset the texture images' info to zeros.
          * Strictly speaking, we probably don't have to do this since
          * generating GL_OUT_OF_MEMORY can leave things in an undefined
          * state but this puts things in a consistent state.
          */
         clear_texture_fields(ctx, texObj);
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTex%sStorage%uD",
                     suffix, dims);
         return;
      }

      _mesa_set_texture_view_state(ctx, texObj, target, levels);

      update_fbo_texture(ctx, texObj);
   }
}


/**
 * Helper used by _mesa_TexStorage1/2/3D().
 */
static void
texstorage(GLuint dims, GLenum target, GLsizei levels, GLenum internalformat,
           GLsizei width, GLsizei height, GLsizei depth)
{
   struct gl_texture_object *texObj;
   GET_CURRENT_CONTEXT(ctx);

   /* Check target.  This is done here so that _mesa_texture_storage
    * can receive unsized formats.
    */
   if (!legal_texobj_target(ctx, dims, target)) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glTexStorage%uD(illegal target=%s)",
                  dims, _mesa_enum_to_string(target));
      return;
   }

   if (MESA_VERBOSE & (VERBOSE_API|VERBOSE_TEXTURE))
      _mesa_debug(ctx, "glTexStorage%uD %s %d %s %d %d %d\n",
                  dims,
                  _mesa_enum_to_string(target), levels,
                  _mesa_enum_to_string(internalformat),
                  width, height, depth);

   /* Check the format to make sure it is sized. */
   if (!_mesa_is_legal_tex_storage_format(ctx, internalformat)) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glTexStorage%uD(internalformat = %s)", dims,
                  _mesa_enum_to_string(internalformat));
      return;
   }

   texObj = _mesa_get_current_tex_object(ctx, target);
   if (!texObj)
      return;

   _mesa_texture_storage(ctx, dims, texObj, target, levels,
                         internalformat, width, height, depth, false);
}


/**
 * Helper used by _mesa_TextureStorage1/2/3D().
 */
static void
texturestorage(GLuint dims, GLuint texture, GLsizei levels,
               GLenum internalformat, GLsizei width, GLsizei height,
               GLsizei depth)
{
   struct gl_texture_object *texObj;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & (VERBOSE_API|VERBOSE_TEXTURE))
      _mesa_debug(ctx, "glTextureStorage%uD %d %d %s %d %d %d\n",
                  dims, texture, levels,
                  _mesa_enum_to_string(internalformat),
                  width, height, depth);

   /* Check the format to make sure it is sized. */
   if (!_mesa_is_legal_tex_storage_format(ctx, internalformat)) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glTextureStorage%uD(internalformat = %s)", dims,
                  _mesa_enum_to_string(internalformat));
      return;
   }

   /* Get the texture object by Name. */
   texObj = _mesa_lookup_texture(ctx, texture);
   if (!texObj) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glTextureStorage%uD(texture = %d)", dims, texture);
      return;
   }

   /* Check target.  This is done here so that _mesa_texture_storage
    * can receive unsized formats.
    */
   if (!legal_texobj_target(ctx, dims, texObj->Target)) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glTextureStorage%uD(illegal target=%s)",
                  dims, _mesa_enum_to_string(texObj->Target));
      return;
   }

   _mesa_texture_storage(ctx, dims, texObj, texObj->Target,
                         levels, internalformat, width, height, depth, true);
}


void GLAPIENTRY
_mesa_TexStorage1D(GLenum target, GLsizei levels, GLenum internalformat,
                   GLsizei width)
{
   texstorage(1, target, levels, internalformat, width, 1, 1);
}


void GLAPIENTRY
_mesa_TexStorage2D(GLenum target, GLsizei levels, GLenum internalformat,
                   GLsizei width, GLsizei height)
{
   texstorage(2, target, levels, internalformat, width, height, 1);
}


void GLAPIENTRY
_mesa_TexStorage3D(GLenum target, GLsizei levels, GLenum internalformat,
                   GLsizei width, GLsizei height, GLsizei depth)
{
   texstorage(3, target, levels, internalformat, width, height, depth);
}


void GLAPIENTRY
_mesa_TextureStorage1D(GLuint texture, GLsizei levels, GLenum internalformat,
                       GLsizei width)
{
   texturestorage(1, texture, levels, internalformat, width, 1, 1);
}


void GLAPIENTRY
_mesa_TextureStorage2D(GLuint texture, GLsizei levels,
                       GLenum internalformat,
                       GLsizei width, GLsizei height)
{
   texturestorage(2, texture, levels, internalformat, width, height, 1);
}


void GLAPIENTRY
_mesa_TextureStorage3D(GLuint texture, GLsizei levels, GLenum internalformat,
                       GLsizei width, GLsizei height, GLsizei depth)
{
   texturestorage(3, texture, levels, internalformat, width, height, depth);
}


/*
 * Note: we don't support GL_EXT_direct_state_access and the spec says
 * we don't need the following functions.  However, glew checks for the
 * presence of all six functions and will say that GL_ARB_texture_storage
 * is not supported if these functions are missing.
 */


void GLAPIENTRY
_mesa_TextureStorage1DEXT(GLuint texture, GLenum target, GLsizei levels,
                          GLenum internalformat,
                          GLsizei width)
{
   GET_CURRENT_CONTEXT(ctx);

   (void) texture;
   (void) target;
   (void) levels;
   (void) internalformat;
   (void) width;

   _mesa_error(ctx, GL_INVALID_OPERATION,
               "glTextureStorage1DEXT not supported");
}


void GLAPIENTRY
_mesa_TextureStorage2DEXT(GLuint texture, GLenum target, GLsizei levels,
                          GLenum internalformat,
                          GLsizei width, GLsizei height)
{
   GET_CURRENT_CONTEXT(ctx);

   (void) texture;
   (void) target;
   (void) levels;
   (void) internalformat;
   (void) width;
   (void) height;

   _mesa_error(ctx, GL_INVALID_OPERATION,
               "glTextureStorage2DEXT not supported");
}


void GLAPIENTRY
_mesa_TextureStorage3DEXT(GLuint texture, GLenum target, GLsizei levels,
                          GLenum internalformat,
                          GLsizei width, GLsizei height, GLsizei depth)
{
   GET_CURRENT_CONTEXT(ctx);

   (void) texture;
   (void) target;
   (void) levels;
   (void) internalformat;
   (void) width;
   (void) height;
   (void) depth;

   _mesa_error(ctx, GL_INVALID_OPERATION,
               "glTextureStorage3DEXT not supported");
}
