/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2010 LunarG Inc.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Chia-I Wu <olv@lunarg.com>
 */

#ifndef ST_CB_EGLIMAGE_H
#define ST_CB_EGLIMAGE_H


struct dd_function_table;

extern void
st_init_eglimage_functions(struct dd_function_table *functions,
                           bool has_egl_image_validate);

void st_egl_image_target_texture_2d(struct gl_context *ctx, GLenum target,
                                    struct gl_texture_object *texObj,
                                    struct gl_texture_image *texImage,
                                    GLeglImageOES image_handle);
void st_egl_image_target_tex_storage(struct gl_context *ctx, GLenum target,
                                     struct gl_texture_object *texObj,
                                     struct gl_texture_image *texImage,
                                     GLeglImageOES image_handle);
void st_egl_image_target_renderbuffer_storage(struct gl_context *ctx,
                                              struct gl_renderbuffer *rb,
                                              GLeglImageOES image_handle);
#endif /* ST_CB_EGLIMAGE_H */
