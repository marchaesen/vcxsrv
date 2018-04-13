/*
 * Copyright © 2017 Valve Corporation.
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
 */

#ifndef TEXTUREBINDLESS_H
#define TEXTUREBINDLESS_H

#include "glheader.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gl_context;

/**
 * \name Internal functions
 */
/*@{*/

void
_mesa_init_resident_handles(struct gl_context *ctx);
void
_mesa_free_resident_handles(struct gl_context *ctx);

void
_mesa_init_shared_handles(struct gl_shared_state *shared);
void
_mesa_free_shared_handles(struct gl_shared_state *shared);

void
_mesa_init_texture_handles(struct gl_texture_object *texObj);
void
_mesa_make_texture_handles_non_resident(struct gl_context *ctx,
                                        struct gl_texture_object *texObj);
void
_mesa_delete_texture_handles(struct gl_context *ctx,
                             struct gl_texture_object *texObj);

void
_mesa_init_sampler_handles(struct gl_sampler_object *sampObj);
void
_mesa_delete_sampler_handles(struct gl_context *ctx,
                             struct gl_sampler_object *sampObj);

/*@}*/

/**
 * \name API functions
 */
/*@{*/

GLuint64 GLAPIENTRY
_mesa_GetTextureHandleARB_no_error(GLuint texture);

GLuint64 GLAPIENTRY
_mesa_GetTextureHandleARB(GLuint texture);

GLuint64 GLAPIENTRY
_mesa_GetTextureSamplerHandleARB_no_error(GLuint texture, GLuint sampler);

GLuint64 GLAPIENTRY
_mesa_GetTextureSamplerHandleARB(GLuint texture, GLuint sampler);

void GLAPIENTRY
_mesa_MakeTextureHandleResidentARB_no_error(GLuint64 handle);

void GLAPIENTRY
_mesa_MakeTextureHandleResidentARB(GLuint64 handle);

void GLAPIENTRY
_mesa_MakeTextureHandleNonResidentARB_no_error(GLuint64 handle);

void GLAPIENTRY
_mesa_MakeTextureHandleNonResidentARB(GLuint64 handle);

GLuint64 GLAPIENTRY
_mesa_GetImageHandleARB_no_error(GLuint texture, GLint level, GLboolean layered,
                                 GLint layer, GLenum format);

GLuint64 GLAPIENTRY
_mesa_GetImageHandleARB(GLuint texture, GLint level, GLboolean layered,
                        GLint layer, GLenum format);

void GLAPIENTRY
_mesa_MakeImageHandleResidentARB_no_error(GLuint64 handle, GLenum access);

void GLAPIENTRY
_mesa_MakeImageHandleResidentARB(GLuint64 handle, GLenum access);

void GLAPIENTRY
_mesa_MakeImageHandleNonResidentARB_no_error(GLuint64 handle);

void GLAPIENTRY
_mesa_MakeImageHandleNonResidentARB(GLuint64 handle);

GLboolean GLAPIENTRY
_mesa_IsTextureHandleResidentARB_no_error(GLuint64 handle);

GLboolean GLAPIENTRY
_mesa_IsTextureHandleResidentARB(GLuint64 handle);

GLboolean GLAPIENTRY
_mesa_IsImageHandleResidentARB_no_error(GLuint64 handle);

GLboolean GLAPIENTRY
_mesa_IsImageHandleResidentARB(GLuint64 handle);

/*@}*/

#ifdef __cplusplus
}
#endif

#endif
