/**
 * \file blend.h
 * Blending functions operations.
 */

/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
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



#ifndef BLEND_H
#define BLEND_H


#include "glheader.h"
#include "context.h"
#include "formats.h"
#include "extensions.h"

struct gl_context;
struct gl_framebuffer;


extern void GLAPIENTRY
_mesa_BlendFunc( GLenum sfactor, GLenum dfactor );

extern void GLAPIENTRY
_mesa_BlendFunc_no_error(GLenum sfactor, GLenum dfactor);

extern void GLAPIENTRY
_mesa_BlendFuncSeparate( GLenum sfactorRGB, GLenum dfactorRGB,
                            GLenum sfactorA, GLenum dfactorA );

extern void GLAPIENTRY
_mesa_BlendFuncSeparate_no_error(GLenum sfactorRGB, GLenum dfactorRGB,
                                 GLenum sfactorA, GLenum dfactorA);

extern void GLAPIENTRY
_mesa_BlendFunciARB_no_error(GLuint buf, GLenum sfactor, GLenum dfactor);
extern void GLAPIENTRY
_mesa_BlendFunciARB(GLuint buf, GLenum sfactor, GLenum dfactor);


extern void GLAPIENTRY
_mesa_BlendFuncSeparateiARB_no_error(GLuint buf, GLenum sfactorRGB,
                                     GLenum dfactorRGB, GLenum sfactorA,
                                     GLenum dfactorA);
extern void GLAPIENTRY
_mesa_BlendFuncSeparateiARB(GLuint buf, GLenum sfactorRGB, GLenum dfactorRGB,
                         GLenum sfactorA, GLenum dfactorA);


extern void GLAPIENTRY
_mesa_BlendEquation( GLenum mode );


void GLAPIENTRY
_mesa_BlendEquationiARB_no_error(GLuint buf, GLenum mode);

extern void GLAPIENTRY
_mesa_BlendEquationiARB(GLuint buf, GLenum mode);


void GLAPIENTRY
_mesa_BlendEquationSeparate_no_error(GLenum modeRGB, GLenum modeA);

extern void GLAPIENTRY
_mesa_BlendEquationSeparate( GLenum modeRGB, GLenum modeA );


extern void GLAPIENTRY
_mesa_BlendEquationSeparateiARB_no_error(GLuint buf, GLenum modeRGB,
                                         GLenum modeA);
extern void GLAPIENTRY
_mesa_BlendEquationSeparateiARB(GLuint buf, GLenum modeRGB, GLenum modeA);


extern void GLAPIENTRY
_mesa_BlendColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);


extern void GLAPIENTRY
_mesa_AlphaFunc( GLenum func, GLclampf ref );


extern void GLAPIENTRY
_mesa_LogicOp( GLenum opcode );


extern void GLAPIENTRY
_mesa_LogicOp_no_error(GLenum opcode);


extern void GLAPIENTRY
_mesa_IndexMask( GLuint mask );

extern void GLAPIENTRY
_mesa_ColorMask( GLboolean red, GLboolean green,
                 GLboolean blue, GLboolean alpha );

extern void GLAPIENTRY
_mesa_ColorMaski( GLuint buf, GLboolean red, GLboolean green,
                        GLboolean blue, GLboolean alpha );


extern void GLAPIENTRY
_mesa_ClampColor(GLenum target, GLenum clamp);

extern GLboolean
_mesa_get_clamp_fragment_color(const struct gl_context *ctx,
                               const struct gl_framebuffer *drawFb);

extern GLboolean
_mesa_get_clamp_vertex_color(const struct gl_context *ctx,
                             const struct gl_framebuffer *drawFb);

extern GLboolean
_mesa_get_clamp_read_color(const struct gl_context *ctx,
                           const struct gl_framebuffer *readFb);

extern void
_mesa_update_clamp_fragment_color(struct gl_context *ctx,
                                  const struct gl_framebuffer *drawFb);

extern void
_mesa_update_clamp_vertex_color(struct gl_context *ctx,
                                const struct gl_framebuffer *drawFb);

extern mesa_format
_mesa_get_render_format(const struct gl_context *ctx, mesa_format format);

extern void  
_mesa_init_color( struct gl_context * ctx );


static inline enum gl_advanced_blend_mode
_mesa_get_advanced_blend_sh_constant(GLbitfield blend_enabled,
                                     enum gl_advanced_blend_mode mode)
{
   return blend_enabled ? mode : BLEND_NONE;
}

static inline bool
_mesa_advanded_blend_sh_constant_changed(struct gl_context *ctx,
                                         GLbitfield new_blend_enabled,
                                         enum gl_advanced_blend_mode new_mode)
{
   return _mesa_get_advanced_blend_sh_constant(new_blend_enabled, new_mode) !=
          _mesa_get_advanced_blend_sh_constant(ctx->Color.BlendEnabled,
                                               ctx->Color._AdvancedBlendMode);
}

static inline void
_mesa_flush_vertices_for_blend_state(struct gl_context *ctx)
{
   if (!ctx->DriverFlags.NewBlend) {
      FLUSH_VERTICES(ctx, _NEW_COLOR);
   } else {
      FLUSH_VERTICES(ctx, 0);
      ctx->NewDriverState |= ctx->DriverFlags.NewBlend;
   }
}

static inline void
_mesa_flush_vertices_for_blend_adv(struct gl_context *ctx,
                                   GLbitfield new_blend_enabled,
                                   enum gl_advanced_blend_mode new_mode)
{
   /* The advanced blend mode needs _NEW_COLOR to update the state constant. */
   if (_mesa_has_KHR_blend_equation_advanced(ctx) &&
       _mesa_advanded_blend_sh_constant_changed(ctx, new_blend_enabled,
                                                new_mode)) {
      FLUSH_VERTICES(ctx, _NEW_COLOR);
      ctx->NewDriverState |= ctx->DriverFlags.NewBlend;
      return;
   }
   _mesa_flush_vertices_for_blend_state(ctx);
}

#endif
