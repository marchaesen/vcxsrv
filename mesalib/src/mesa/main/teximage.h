/**
 * \file teximage.h
 * Texture images manipulation functions.
 */

/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
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


#ifndef TEXIMAGE_H
#define TEXIMAGE_H


#include "mtypes.h"
#include "formats.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Is the given value one of the 6 cube faces? */
static inline GLboolean
_mesa_is_cube_face(GLenum target)
{
   return (target >= GL_TEXTURE_CUBE_MAP_POSITIVE_X &&
           target <= GL_TEXTURE_CUBE_MAP_NEGATIVE_Z);
}


/**
 * Return number of faces for a texture target.  This will be 6 for
 * cube maps and 1 otherwise.
 * NOTE: this function is not used for cube map arrays which operate
 * more like 2D arrays than cube maps.
 */
static inline GLuint
_mesa_num_tex_faces(GLenum target)
{
   switch (target) {
   case GL_TEXTURE_CUBE_MAP:
   case GL_PROXY_TEXTURE_CUBE_MAP:
      return 6;
   default:
      return 1;
   }
}


/**
 * If the target is GL_TEXTURE_CUBE_MAP, return one of the
 * GL_TEXTURE_CUBE_MAP_POSITIVE/NEGATIVE_X/Y/Z targets corresponding to
 * the face parameter.
 * Else, return target as-is.
 */
static inline GLenum
_mesa_cube_face_target(GLenum target, unsigned face)
{
   if (target == GL_TEXTURE_CUBE_MAP) {
      assert(face < 6);
      return GL_TEXTURE_CUBE_MAP_POSITIVE_X + face;
   }
   else {
      return target;
   }
}


/**
 * For cube map faces, return a face index in [0,5].
 * For other targets return 0;
 */
static inline GLuint
_mesa_tex_target_to_face(GLenum target)
{
   if (_mesa_is_cube_face(target))
      return (GLuint) target - (GLuint) GL_TEXTURE_CUBE_MAP_POSITIVE_X;
   else
      return 0;
}


/** Are any of the dimensions of given texture equal to zero? */
static inline GLboolean
_mesa_is_zero_size_texture(const struct gl_texture_image *texImage)
{
   return (texImage->Width == 0 ||
           texImage->Height == 0 ||
           texImage->Depth == 0);
}

/** \name Internal functions */
/*@{*/

extern GLboolean
_mesa_is_proxy_texture(GLenum target);

extern bool
_mesa_is_array_texture(GLenum target);


extern void
_mesa_delete_texture_image( struct gl_context *ctx,
                            struct gl_texture_image *teximage );


extern void
_mesa_init_teximage_fields(struct gl_context *ctx,
                           struct gl_texture_image *img,
                           GLsizei width, GLsizei height, GLsizei depth,
                           GLint border, GLenum internalFormat,
                           mesa_format format);
extern void
_mesa_init_teximage_fields_ms(struct gl_context *ctx,
                              struct gl_texture_image *img,
                              GLsizei width, GLsizei height, GLsizei depth,
                              GLint border, GLenum internalFormat,
                              mesa_format format,
                              GLuint numSamples,
                              GLboolean fixedSampleLocations);


extern mesa_format
_mesa_choose_texture_format(struct gl_context *ctx,
                            struct gl_texture_object *texObj,
                            GLenum target, GLint level,
                            GLenum internalFormat, GLenum format, GLenum type);

extern void
_mesa_update_fbo_texture(struct gl_context *ctx,
                         struct gl_texture_object *texObj,
                         GLuint face, GLuint level);

extern void
_mesa_clear_texture_image(struct gl_context *ctx,
                          struct gl_texture_image *texImage);


extern struct gl_texture_image *
_mesa_select_tex_image(const struct gl_texture_object *texObj,
                       GLenum target, GLint level);


extern struct gl_texture_image *
_mesa_get_tex_image(struct gl_context *ctx, struct gl_texture_object *texObj,
                    GLenum target, GLint level);

mesa_format
_mesa_get_texbuffer_format(const struct gl_context *ctx, GLenum internalFormat);

/**
 * Return the base-level texture image for the given texture object.
 */
static inline const struct gl_texture_image *
_mesa_base_tex_image(const struct gl_texture_object *texObj)
{
   return texObj->Image[0][texObj->BaseLevel];
}


extern GLint
_mesa_max_texture_levels(const struct gl_context *ctx, GLenum target);


extern GLboolean
_mesa_test_proxy_teximage(struct gl_context *ctx, GLenum target,
                          GLuint numLevels, GLint level,
                          mesa_format format, GLuint numSamples,
                          GLint width, GLint height, GLint depth);

extern GLboolean
_mesa_target_can_be_compressed(const struct gl_context *ctx, GLenum target,
                               GLenum intFormat, GLenum *error);

extern GLint
_mesa_get_texture_dimensions(GLenum target);

extern GLboolean
_mesa_tex_target_is_layered(GLenum target);

extern GLuint
_mesa_get_texture_layers(const struct gl_texture_object *texObj, GLint level);

extern GLsizei
_mesa_get_tex_max_num_levels(GLenum target, GLsizei width, GLsizei height,
                             GLsizei depth);

extern GLboolean
_mesa_legal_texture_dimensions(struct gl_context *ctx, GLenum target,
                               GLint level, GLint width, GLint height,
                               GLint depth, GLint border);

extern mesa_format
_mesa_validate_texbuffer_format(const struct gl_context *ctx,
                                GLenum internalFormat);


bool
_mesa_legal_texture_base_format_for_target(struct gl_context *ctx,
                                           GLenum target,
                                           GLenum internalFormat);

bool
_mesa_format_no_online_compression(GLenum format);

GLboolean
_mesa_is_renderable_texture_format(const struct gl_context *ctx,
                                   GLenum internalformat);

extern void
_mesa_texture_sub_image(struct gl_context *ctx, GLuint dims,
                        struct gl_texture_object *texObj,
                        struct gl_texture_image *texImage,
                        GLenum target, GLint level,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLsizei depth,
                        GLenum format, GLenum type, const GLvoid *pixels,
                        bool dsa);

extern void
_mesa_texture_storage_ms_memory(struct gl_context *ctx, GLuint dims,
                                struct gl_texture_object *texObj,
                                struct gl_memory_object *memObj,
                                GLenum target, GLsizei samples,
                                GLenum internalFormat, GLsizei width,
                                GLsizei height, GLsizei depth,
                                GLboolean fixedSampleLocations,
                                GLuint64 offset, const char* func);

bool
_mesa_is_cube_map_texture(GLenum target);

/*@}*/


/** \name API entry point functions */
/*@{*/

extern void GLAPIENTRY
_mesa_TexImage1D( GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TextureImage1DEXT( GLuint texture, GLenum target, GLint level,
                         GLint internalformat, GLsizei width, GLint border,
                         GLenum format, GLenum type, const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_MultiTexImage1DEXT( GLenum texture, GLenum target, GLint level,
                          GLint internalformat, GLsizei width, GLint border,
                          GLenum format, GLenum type, const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TexImage2D( GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TextureImage2DEXT( GLuint texture, GLenum target, GLint level,
                         GLint internalformat, GLsizei width, GLsizei height,
                         GLint border, GLenum format, GLenum type,
                         const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_MultiTexImage2DEXT(GLenum texture, GLenum target, GLint level,
                         GLint internalFormat, GLsizei width, GLsizei height,
                         GLint border, GLenum format, GLenum type,
                         const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TexImage3D( GLenum target, GLint level, GLint internalformat,
                  GLsizei width, GLsizei height, GLsizei depth, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TextureImage3DEXT( GLuint texture, GLenum target, GLint level,
                         GLint internalformat, GLsizei width, GLsizei height,
                         GLsizei depth, GLint border, GLenum format,
                         GLenum type, const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TexImage3DEXT( GLenum target, GLint level, GLenum internalformat,
                     GLsizei width, GLsizei height, GLsizei depth,
                     GLint border, GLenum format, GLenum type,
                     const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_MultiTexImage3DEXT(GLenum texture, GLenum target, GLint level,
                         GLint internalFormat, GLsizei width, GLsizei height,
                         GLsizei depth, GLint border, GLenum format, GLenum type,
                         const GLvoid *pixels );

extern void GLAPIENTRY
_mesa_TexImage1D_no_error(GLenum target, GLint level, GLint internalformat,
                          GLsizei width, GLint border,
                          GLenum format, GLenum type, const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TexImage2D_no_error(GLenum target, GLint level, GLint internalformat,
                          GLsizei width, GLsizei height, GLint border,
                          GLenum format, GLenum type, const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TexImage3D_no_error(GLenum target, GLint level, GLint internalformat,
                          GLsizei width, GLsizei height, GLsizei depth,
                          GLint border, GLenum format, GLenum type,
                          const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_EGLImageTargetTexture2DOES( GLenum target, GLeglImageOES image );

extern void GLAPIENTRY
_mesa_EGLImageTargetTexStorageEXT(GLenum target, GLeglImageOES image,
                                  const GLint *attrib_list);
extern void GLAPIENTRY
_mesa_EGLImageTargetTextureStorageEXT(GLuint texture, GLeglImageOES image,
                                      const GLint *attrib_list);
void GLAPIENTRY
_mesa_TexSubImage1D_no_error(GLenum target, GLint level, GLint xoffset,
                             GLsizei width,
                             GLenum format, GLenum type,
                             const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TexSubImage1D( GLenum target, GLint level, GLint xoffset,
                     GLsizei width,
                     GLenum format, GLenum type,
                     const GLvoid *pixels );

void GLAPIENTRY
_mesa_TexSubImage2D_no_error(GLenum target, GLint level,
                             GLint xoffset, GLint yoffset,
                             GLsizei width, GLsizei height,
                             GLenum format, GLenum type,
                             const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TexSubImage2D( GLenum target, GLint level,
                     GLint xoffset, GLint yoffset,
                     GLsizei width, GLsizei height,
                     GLenum format, GLenum type,
                     const GLvoid *pixels );

void GLAPIENTRY
_mesa_TexSubImage3D_no_error(GLenum target, GLint level,
                             GLint xoffset, GLint yoffset, GLint zoffset,
                             GLsizei width, GLsizei height, GLsizei depth,
                             GLenum format, GLenum type,
                             const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TexSubImage3D( GLenum target, GLint level,
                     GLint xoffset, GLint yoffset, GLint zoffset,
                     GLsizei width, GLsizei height, GLsizei depth,
                     GLenum format, GLenum type,
                     const GLvoid *pixels );

void GLAPIENTRY
_mesa_TextureSubImage1D_no_error(GLuint texture, GLint level, GLint xoffset,
                                 GLsizei width, GLenum format, GLenum type,
                                 const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TextureSubImage1D(GLuint texture, GLint level, GLint xoffset,
                        GLsizei width,
                        GLenum format, GLenum type,
                        const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TextureSubImage1DEXT(GLuint texture, GLenum target, GLint level,
                        GLint xoffset, GLsizei width,
                        GLenum format, GLenum type,
                        const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_MultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level,
                            GLint xoffset, GLsizei width,
                            GLenum format, GLenum type,
                            const GLvoid *pixels);

void GLAPIENTRY
_mesa_TextureSubImage2DEXT(GLuint texture, GLenum target, GLint level,
                           GLint xoffset, GLint yoffset, GLsizei width,
                           GLsizei height, GLenum format, GLenum type,
                           const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_MultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level,
                            GLint xoffset, GLint yoffset, GLsizei width,
                            GLsizei height, GLenum format, GLenum type,
                            const GLvoid *pixels);

void GLAPIENTRY
_mesa_TextureSubImage2D_no_error(GLuint texture, GLint level, GLint xoffset,
                                 GLint yoffset, GLsizei width, GLsizei height,
                                 GLenum format, GLenum type,
                                 const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TextureSubImage2D(GLuint texture, GLint level,
                        GLint xoffset, GLint yoffset,
                        GLsizei width, GLsizei height,
                        GLenum format, GLenum type,
                        const GLvoid *pixels);

void GLAPIENTRY
_mesa_TextureSubImage3D_no_error(GLuint texture, GLint level, GLint xoffset,
                                 GLint yoffset, GLint zoffset, GLsizei width,
                                 GLsizei height, GLsizei depth, GLenum format,
                                 GLenum type, const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TextureSubImage3D(GLuint texture, GLint level,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLsizei depth,
                        GLenum format, GLenum type,
                        const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_TextureSubImage3DEXT(GLuint texture, GLenum target,
                        GLint level, GLint xoffset, GLint yoffset,
                        GLint zoffset, GLsizei width, GLsizei height,
                        GLsizei depth, GLenum format, GLenum type,
                        const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_MultiTexSubImage3DEXT(GLenum texunit, GLenum target,
                            GLint level, GLint xoffset, GLint yoffset,
                            GLint zoffset, GLsizei width, GLsizei height,
                            GLsizei depth, GLenum format, GLenum type,
                            const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_CopyTexImage1D(GLenum target, GLint level, GLenum internalformat,
                     GLint x, GLint y, GLsizei width, GLint border);

extern void GLAPIENTRY
_mesa_CopyMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level,
                             GLenum internalformat, GLint x, GLint y,
                             GLsizei width, GLint border);

extern void GLAPIENTRY
_mesa_CopyTexImage2D( GLenum target, GLint level,
                      GLenum internalformat, GLint x, GLint y,
                      GLsizei width, GLsizei height, GLint border );

extern void GLAPIENTRY
_mesa_CopyMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level,
                             GLenum internalformat, GLint x, GLint y,
                             GLsizei width, GLsizei hright, GLint border);

extern void GLAPIENTRY
_mesa_CopyTextureImage1DEXT( GLuint texture, GLenum target, GLint level,
                             GLenum internalformat, GLint x, GLint y,
                             GLsizei width, GLint border);

extern void GLAPIENTRY
_mesa_CopyTextureImage2DEXT( GLuint texture, GLenum target, GLint level,
                             GLenum internalformat, GLint x, GLint y,
                             GLsizei width, GLsizei height, GLint border );

extern void GLAPIENTRY
_mesa_CopyTexImage1D_no_error(GLenum target, GLint level, GLenum internalformat,
                              GLint x, GLint y, GLsizei width, GLint border);


extern void GLAPIENTRY
_mesa_CopyTexImage2D_no_error(GLenum target, GLint level, GLenum internalformat,
                              GLint x, GLint y, GLsizei width, GLsizei height,
                              GLint border );


extern void GLAPIENTRY
_mesa_CopyTexSubImage1D( GLenum target, GLint level, GLint xoffset,
                         GLint x, GLint y, GLsizei width );


extern void GLAPIENTRY
_mesa_CopyTexSubImage2D( GLenum target, GLint level,
                         GLint xoffset, GLint yoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height );


extern void GLAPIENTRY
_mesa_CopyTexSubImage3D( GLenum target, GLint level,
                         GLint xoffset, GLint yoffset, GLint zoffset,
                         GLint x, GLint y, GLsizei width, GLsizei height );

extern void GLAPIENTRY
_mesa_CopyTextureSubImage1D(GLuint texture, GLint level,
                            GLint xoffset, GLint x, GLint y, GLsizei width);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage1DEXT(GLuint texture, GLenum target,
                               GLint level, GLint xoffset, GLint x, GLint y,
                               GLsizei width);

extern void GLAPIENTRY
_mesa_CopyMultiTexSubImage1DEXT(GLenum texunit, GLenum target,
                                GLint level, GLint xoffset, GLint x, GLint y,
                                GLsizei width);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage2D(GLuint texture, GLint level,
                            GLint xoffset, GLint yoffset,
                            GLint x, GLint y,
                            GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage2DEXT(GLuint texture, GLenum target, GLint level,
                               GLint xoffset, GLint yoffset,
                               GLint x, GLint y,
                               GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyMultiTexSubImage2DEXT(GLenum texunit, GLenum target, GLint level,
                                GLint xoffset, GLint yoffset,
                                GLint x, GLint y,
                                GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage3D(GLuint texture, GLint level,
                            GLint xoffset, GLint yoffset, GLint zoffset,
                            GLint x, GLint y,
                            GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage3DEXT(GLuint texture, GLenum target, GLint level,
                               GLint xoffset, GLint yoffset, GLint zoffset,
                               GLint x, GLint y,
                               GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyMultiTexSubImage3DEXT(GLenum texunit, GLenum target, GLint level,
                                GLint xoffset, GLint yoffset, GLint zoffset,
                                GLint x, GLint y,
                                GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTexSubImage1D_no_error(GLenum target, GLint level, GLint xoffset,
                                 GLint x, GLint y, GLsizei width );

extern void GLAPIENTRY
_mesa_CopyTexSubImage2D_no_error(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLint x, GLint y, GLsizei width,
                                 GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTexSubImage3D_no_error(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLint zoffset, GLint x, GLint y,
                                 GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage1D_no_error(GLuint texture, GLint level, GLint xoffset,
                                     GLint x, GLint y, GLsizei width);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage2D_no_error(GLuint texture, GLint level, GLint xoffset,
                                     GLint yoffset, GLint x, GLint y,
                                     GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_CopyTextureSubImage3D_no_error(GLuint texture, GLint level, GLint xoffset,
                                     GLint yoffset, GLint zoffset, GLint x,
                                     GLint y, GLsizei width, GLsizei height);

extern void GLAPIENTRY
_mesa_ClearTexSubImage( GLuint texture, GLint level,
                        GLint xoffset, GLint yoffset, GLint zoffset,
                        GLsizei width, GLsizei height, GLsizei depth,
                        GLenum format, GLenum type, const void *data );

extern void GLAPIENTRY
_mesa_ClearTexImage( GLuint texture, GLint level,
                     GLenum format, GLenum type, const void *data );

extern void GLAPIENTRY
_mesa_CompressedTexImage1D(GLenum target, GLint level,
                              GLenum internalformat, GLsizei width,
                              GLint border, GLsizei imageSize,
                              const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureImage1DEXT(GLuint texture, GLenum target, GLint level,
                                  GLenum internalFormat, GLsizei width,
                                  GLint border, GLsizei imageSize,
                                  const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_CompressedMultiTexImage1DEXT(GLenum texunit, GLenum target, GLint level,
                                   GLenum internalFormat, GLsizei width,
                                   GLint border, GLsizei imageSize,
                                   const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_CompressedTexImage2D(GLenum target, GLint level,
                              GLenum internalformat, GLsizei width,
                              GLsizei height, GLint border, GLsizei imageSize,
                              const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureImage2DEXT(GLuint texture, GLenum target, GLint level,
                                  GLenum internalFormat, GLsizei width,
                                  GLsizei height, GLint border, GLsizei imageSize,
                                  const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_CompressedMultiTexImage2DEXT(GLenum texunit, GLenum target, GLint level,
                                   GLenum internalFormat, GLsizei width,
                                   GLsizei height, GLint border, GLsizei imageSize,
                                   const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_CompressedTexImage3D(GLenum target, GLint level,
                              GLenum internalformat, GLsizei width,
                              GLsizei height, GLsizei depth, GLint border,
                              GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureImage3DEXT(GLuint texture, GLenum target, GLint level,
                                  GLenum internalFormat, GLsizei width,
                                  GLsizei height, GLsizei depth, GLint border,
                                  GLsizei imageSize, const GLvoid *pixels);

extern void GLAPIENTRY
_mesa_CompressedMultiTexImage3DEXT(GLenum texunit, GLenum target, GLint level,
                                   GLenum internalFormat, GLsizei width,
                                   GLsizei height, GLsizei depth, GLint border,
                                   GLsizei imageSize, const GLvoid *pixels);


extern void GLAPIENTRY
_mesa_CompressedTexImage1D_no_error(GLenum target, GLint level,
                                    GLenum internalformat, GLsizei width,
                                    GLint border, GLsizei imageSize,
                                    const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTexImage2D_no_error(GLenum target, GLint level,
                                    GLenum internalformat, GLsizei width,
                                    GLsizei height, GLint border,
                                    GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTexImage3D_no_error(GLenum target, GLint level,
                                    GLenum internalformat, GLsizei width,
                                    GLsizei height, GLsizei depth, GLint border,
                                    GLsizei imageSize, const GLvoid *data);


extern void GLAPIENTRY
_mesa_CompressedTexSubImage1D_no_error(GLenum target, GLint level,
                                       GLint xoffset, GLsizei width,
                                       GLenum format, GLsizei imageSize,
                                       const GLvoid *data);
extern void GLAPIENTRY
_mesa_CompressedTexSubImage1D(GLenum target, GLint level, GLint xoffset,
                                 GLsizei width, GLenum format,
                                 GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureSubImage1D_no_error(GLuint texture, GLint level,
                                           GLint xoffset, GLsizei width,
                                           GLenum format, GLsizei imageSize,
                                           const GLvoid *data);
extern void GLAPIENTRY
_mesa_CompressedTextureSubImage1D(GLuint texture, GLint level, GLint xoffset,
                                  GLsizei width, GLenum format,
                                  GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureSubImage1DEXT(GLuint texture, GLenum target, GLint level,
                                     GLint xoffset, GLsizei width, GLenum format,
                                     GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedMultiTexSubImage1DEXT(GLenum texunit, GLenum target, GLint level,
                                      GLint xoffset, GLsizei width, GLenum format,
                                      GLsizei imageSize, const GLvoid *data);


void GLAPIENTRY
_mesa_CompressedTextureSubImage2DEXT(GLuint texture, GLenum target,
                                     GLint level, GLint xoffset,
                                     GLint yoffset, GLsizei width,
                                     GLsizei height, GLenum format,
                                     GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedMultiTexSubImage2DEXT(GLenum texunit, GLenum target,
                                      GLint level, GLint xoffset,
                                      GLint yoffset, GLsizei width,
                                      GLsizei height, GLenum format,
                                      GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureSubImage3DEXT(GLuint texture, GLenum target,
                                     GLint level, GLint xoffset,
                                     GLint yoffset, GLint zoffset,
                                     GLsizei width, GLsizei height, GLsizei depth,
                                     GLenum format, GLsizei imageSize,
                                     const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedMultiTexSubImage3DEXT(GLenum texunit, GLenum target,
                                      GLint level, GLint xoffset,
                                      GLint yoffset, GLint zoffset,
                                      GLsizei width, GLsizei height, GLsizei depth,
                                      GLenum format, GLsizei imageSize,
                                      const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTexSubImage2D_no_error(GLenum target, GLint level,
                                       GLint xoffset, GLint yoffset,
                                       GLsizei width, GLsizei height,
                                       GLenum format, GLsizei imageSize,
                                       const GLvoid *data);
extern void GLAPIENTRY
_mesa_CompressedTexSubImage2D(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLsizei width, GLsizei height,
                                 GLenum format, GLsizei imageSize,
                                 const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureSubImage2D_no_error(GLuint texture, GLint level,
                                           GLint xoffset, GLint yoffset,
                                           GLsizei width, GLsizei height,
                                           GLenum format, GLsizei imageSize,
                                           const GLvoid *data);
extern void GLAPIENTRY
_mesa_CompressedTextureSubImage2D(GLuint texture, GLint level, GLint xoffset,
                                  GLint yoffset,
                                  GLsizei width, GLsizei height,
                                  GLenum format, GLsizei imageSize,
                                  const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTexSubImage3D_no_error(GLenum target, GLint level,
                                       GLint xoffset, GLint yoffset,
                                       GLint zoffset, GLsizei width,
                                       GLsizei height, GLsizei depth,
                                       GLenum format, GLsizei imageSize,
                                       const GLvoid *data);
extern void GLAPIENTRY
_mesa_CompressedTexSubImage3D(GLenum target, GLint level, GLint xoffset,
                                 GLint yoffset, GLint zoffset, GLsizei width,
                                 GLsizei height, GLsizei depth, GLenum format,
                                 GLsizei imageSize, const GLvoid *data);

extern void GLAPIENTRY
_mesa_CompressedTextureSubImage3D_no_error(GLuint texture, GLint level,
                                           GLint xoffset, GLint yoffset,
                                           GLint zoffset, GLsizei width,
                                           GLsizei height, GLsizei depth,
                                           GLenum format, GLsizei imageSize,
                                           const GLvoid *data);
extern void GLAPIENTRY
_mesa_CompressedTextureSubImage3D(GLuint texture, GLint level, GLint xoffset,
                                  GLint yoffset, GLint zoffset,
                                  GLsizei width, GLsizei height,
                                  GLsizei depth,
                                  GLenum format, GLsizei imageSize,
                                  const GLvoid *data);

extern void GLAPIENTRY
_mesa_TexBuffer(GLenum target, GLenum internalFormat, GLuint buffer);

extern void GLAPIENTRY
_mesa_TexBufferRange(GLenum target, GLenum internalFormat, GLuint buffer,
                     GLintptr offset, GLsizeiptr size);

extern void GLAPIENTRY
_mesa_TextureBufferRangeEXT(GLuint texture, GLenum target, GLenum internalFormat,
                            GLuint buffer, GLintptr offset, GLsizeiptr size);

extern void GLAPIENTRY
_mesa_TextureBuffer(GLuint texture, GLenum internalFormat, GLuint buffer);

extern void GLAPIENTRY
_mesa_TextureBufferEXT(GLuint texture, GLenum target, GLenum internalFormat,
                       GLuint buffer);

extern void GLAPIENTRY
_mesa_MultiTexBufferEXT(GLenum texunit, GLenum target, GLenum internalFormat,
                        GLuint buffer);

extern void GLAPIENTRY
_mesa_TextureBufferRange(GLuint texture, GLenum internalFormat, GLuint buffer,
                         GLintptr offset, GLsizeiptr size);


extern void GLAPIENTRY
_mesa_TexImage2DMultisample(GLenum target, GLsizei samples,
                            GLenum internalformat, GLsizei width,
                            GLsizei height, GLboolean fixedsamplelocations);

extern void GLAPIENTRY
_mesa_TexImage3DMultisample(GLenum target, GLsizei samples,
                            GLenum internalformat, GLsizei width,
                            GLsizei height, GLsizei depth,
                            GLboolean fixedsamplelocations);

extern void GLAPIENTRY
_mesa_TexStorage2DMultisample(GLenum target, GLsizei samples,
                              GLenum internalformat, GLsizei width,
                              GLsizei height, GLboolean fixedsamplelocations);

extern void GLAPIENTRY
_mesa_TexStorage3DMultisample(GLenum target, GLsizei samples,
                              GLenum internalformat, GLsizei width,
                              GLsizei height, GLsizei depth,
                              GLboolean fixedsamplelocations);

void GLAPIENTRY
_mesa_TextureStorage2DMultisample(GLuint texture, GLsizei samples,
                                  GLenum internalformat, GLsizei width,
                                  GLsizei height,
                                  GLboolean fixedsamplelocations);

void GLAPIENTRY
_mesa_TextureStorage3DMultisample(GLuint texture, GLsizei samples,
                                  GLenum internalformat, GLsizei width,
                                  GLsizei height, GLsizei depth,
                                  GLboolean fixedsamplelocations);

extern void GLAPIENTRY
_mesa_TextureStorage2DMultisampleEXT(GLuint texture, GLenum target, GLsizei samples,
                                     GLenum internalformat, GLsizei width,
                                     GLsizei height, GLboolean fixedsamplelocations);

extern void GLAPIENTRY
_mesa_TextureStorage3DMultisampleEXT(GLuint texture, GLenum target, GLsizei samples,
                                     GLenum internalformat, GLsizei width,
                                     GLsizei height, GLsizei depth,
                                     GLboolean fixedsamplelocations);

/*@}*/

#ifdef __cplusplus
}
#endif

#endif
