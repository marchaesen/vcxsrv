/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
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

#include "main/glheader.h"
#include "main/mtypes.h"
#include "main/imports.h"
#include "main/arbprogram.h"
#include "main/arrayobj.h"
#include "main/blend.h"
#include "main/condrender.h"
#include "main/depth.h"
#include "main/enable.h"
#include "main/enums.h"
#include "main/fbobject.h"
#include "main/image.h"
#include "main/macros.h"
#include "main/matrix.h"
#include "main/multisample.h"
#include "main/objectlabel.h"
#include "main/readpix.h"
#include "main/scissor.h"
#include "main/shaderapi.h"
#include "main/texobj.h"
#include "main/texenv.h"
#include "main/teximage.h"
#include "main/texparam.h"
#include "main/uniforms.h"
#include "main/varray.h"
#include "main/viewport.h"
#include "swrast/swrast.h"
#include "drivers/common/meta.h"
#include "util/ralloc.h"

/** Return offset in bytes of the field within a vertex struct */
#define OFFSET(FIELD) ((void *) offsetof(struct vertex, FIELD))

static void
setup_glsl_msaa_blit_scaled_shader(struct gl_context *ctx,
                                   struct blit_state *blit,
                                   struct gl_renderbuffer *src_rb,
                                   GLenum target, GLenum filter)
{
   GLint loc_src_width, loc_src_height;
   int i, samples;
   int shader_offset = 0;
   void *mem_ctx = ralloc_context(NULL);
   char *fs_source;
   char *name, *sample_number;
   const uint8_t *sample_map;
   char *sample_map_str = rzalloc_size(mem_ctx, 1);
   char *sample_map_expr = rzalloc_size(mem_ctx, 1);
   char *texel_fetch_macro = rzalloc_size(mem_ctx, 1);
   const char *sampler_array_suffix = "";
   float x_scale, y_scale;
   enum blit_msaa_shader shader_index;

   assert(src_rb);
   samples = MAX2(src_rb->NumSamples, 1);

   if (samples == 16)
      x_scale = 4.0;
   else
      x_scale = 2.0;
   y_scale = samples / x_scale;

   /* We expect only power of 2 samples in source multisample buffer. */
   assert(samples > 0 && _mesa_is_pow_two(samples));
   while (samples >> (shader_offset + 1)) {
      shader_offset++;
   }
   /* Update the assert if we plan to support more than 16X MSAA. */
   assert(shader_offset > 0 && shader_offset <= 4);

   assert(target == GL_TEXTURE_2D_MULTISAMPLE ||
          target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY);

   shader_index = BLIT_2X_MSAA_SHADER_2D_MULTISAMPLE_SCALED_RESOLVE +
                  shader_offset - 1;

   if (target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
      shader_index += BLIT_2X_MSAA_SHADER_2D_MULTISAMPLE_ARRAY_SCALED_RESOLVE -
                      BLIT_2X_MSAA_SHADER_2D_MULTISAMPLE_SCALED_RESOLVE;
      sampler_array_suffix = "Array";
   }

   if (blit->msaa_shaders[shader_index]) {
      _mesa_UseProgram(blit->msaa_shaders[shader_index]);
      /* Update the uniform values. */
      loc_src_width =
         _mesa_GetUniformLocation(blit->msaa_shaders[shader_index], "src_width");
      loc_src_height =
         _mesa_GetUniformLocation(blit->msaa_shaders[shader_index], "src_height");
      _mesa_Uniform1f(loc_src_width, src_rb->Width);
      _mesa_Uniform1f(loc_src_height, src_rb->Height);
      return;
   }

   name = ralloc_asprintf(mem_ctx, "vec4 MSAA scaled resolve");

   /* Below switch is used to setup the shader expression, which computes
    * sample index and map it to to a sample number on hardware.
    */
   switch(samples) {
   case 2:
      sample_number =  "sample_map[int(2 * fract(coord.x))]";
      sample_map = ctx->Const.SampleMap2x;
      break;
   case 4:
      sample_number =  "sample_map[int(2 * fract(coord.x) + 4 * fract(coord.y))]";
      sample_map = ctx->Const.SampleMap4x;
      break;
   case 8:
      sample_number =  "sample_map[int(2 * fract(coord.x) + 8 * fract(coord.y))]";
      sample_map = ctx->Const.SampleMap8x;
      break;
   case 16:
      sample_number =  "sample_map[int(4 * fract(coord.x) + 16 * fract(coord.y))]";
      sample_map = ctx->Const.SampleMap16x;
      break;
   default:
      sample_number = NULL;
      sample_map = NULL;
      _mesa_problem(ctx, "Unsupported sample count %d\n", samples);
      unreachable("Unsupported sample count");
   }

   /* Create sample map string. */
   for (i = 0 ; i < samples - 1; i++) {
      ralloc_asprintf_append(&sample_map_str, "%d, ", sample_map[i]);
   }
   ralloc_asprintf_append(&sample_map_str, "%d", sample_map[samples - 1]);

   /* Create sample map expression using above string. */
   ralloc_asprintf_append(&sample_map_expr,
                          "   const int sample_map[%d] = int[%d](%s);\n",
                          samples, samples, sample_map_str);

   if (target == GL_TEXTURE_2D_MULTISAMPLE) {
      ralloc_asprintf_append(&texel_fetch_macro,
                             "#define TEXEL_FETCH(coord) texelFetch(texSampler, ivec2(coord), %s);\n",
                             sample_number);
   } else {
      ralloc_asprintf_append(&texel_fetch_macro,
                             "#define TEXEL_FETCH(coord) texelFetch(texSampler, ivec3(coord, layer), %s);\n",
                             sample_number);
   }

   static const char vs_source[] =
                               "#version 130\n"
                               "in vec2 position;\n"
                               "in vec3 textureCoords;\n"
                               "out vec2 texCoords;\n"
                               "flat out int layer;\n"
                               "void main()\n"
                               "{\n"
                               "   texCoords = textureCoords.xy;\n"
                               "   layer = int(textureCoords.z);\n"
                               "   gl_Position = vec4(position, 0.0, 1.0);\n"
                               "}\n"
      ;

   fs_source = ralloc_asprintf(mem_ctx,
                               "#version 130\n"
                               "#extension GL_ARB_texture_multisample : enable\n"
                               "uniform sampler2DMS%s texSampler;\n"
                               "uniform float src_width, src_height;\n"
                               "in vec2 texCoords;\n"
                               "flat in int layer;\n"
                               "out vec4 out_color;\n"
                               "\n"
                               "void main()\n"
                               "{\n"
                               "%s"
                               "   vec2 interp;\n"
                               "   const vec2 scale = vec2(%ff, %ff);\n"
                               "   const vec2 scale_inv = vec2(%ff, %ff);\n"
                               "   const vec2 s_0_offset = vec2(%ff, %ff);\n"
                               "   vec2 s_0_coord, s_1_coord, s_2_coord, s_3_coord;\n"
                               "   vec4 s_0_color, s_1_color, s_2_color, s_3_color;\n"
                               "   vec4 x_0_color, x_1_color;\n"
                               "   vec2 tex_coord = texCoords - s_0_offset;\n"
                               "\n"
                               "   tex_coord *= scale;\n"
                               "   tex_coord.x = clamp(tex_coord.x, 0.0f, scale.x * src_width - 1.0f);\n"
                               "   tex_coord.y = clamp(tex_coord.y, 0.0f, scale.y * src_height - 1.0f);\n"
                               "   interp = fract(tex_coord);\n"
                               "   tex_coord = ivec2(tex_coord) * scale_inv;\n"
                               "\n"
                               "   /* Compute the sample coordinates used for filtering. */\n"
                               "   s_0_coord = tex_coord;\n"
                               "   s_1_coord = tex_coord + vec2(scale_inv.x, 0.0f);\n"
                               "   s_2_coord = tex_coord + vec2(0.0f, scale_inv.y);\n"
                               "   s_3_coord = tex_coord + vec2(scale_inv.x, scale_inv.y);\n"
                               "\n"
                               "   /* Fetch sample color values. */\n"
                               "%s"
                               "   s_0_color = TEXEL_FETCH(s_0_coord)\n"
                               "   s_1_color = TEXEL_FETCH(s_1_coord)\n"
                               "   s_2_color = TEXEL_FETCH(s_2_coord)\n"
                               "   s_3_color = TEXEL_FETCH(s_3_coord)\n"
                               "#undef TEXEL_FETCH\n"
                               "\n"
                               "   /* Do bilinear filtering on sample colors. */\n"
                               "   x_0_color =  mix(s_0_color, s_1_color, interp.x);\n"
                               "   x_1_color =  mix(s_2_color, s_3_color, interp.x);\n"
                               "   out_color = mix(x_0_color, x_1_color, interp.y);\n"
                               "}\n",
                               sampler_array_suffix,
                               sample_map_expr,
                               x_scale, y_scale,
                               1.0f / x_scale, 1.0f / y_scale,
                               0.5f / x_scale, 0.5f / y_scale,
                               texel_fetch_macro);

   _mesa_meta_compile_and_link_program(ctx, vs_source, fs_source, name,
                                       &blit->msaa_shaders[shader_index]);
   loc_src_width =
      _mesa_GetUniformLocation(blit->msaa_shaders[shader_index], "src_width");
   loc_src_height =
      _mesa_GetUniformLocation(blit->msaa_shaders[shader_index], "src_height");
   _mesa_Uniform1f(loc_src_width, src_rb->Width);
   _mesa_Uniform1f(loc_src_height, src_rb->Height);

   ralloc_free(mem_ctx);
}

static void
setup_glsl_msaa_blit_shader(struct gl_context *ctx,
                            struct blit_state *blit,
                            const struct gl_framebuffer *drawFb,
                            struct gl_renderbuffer *src_rb,
                            GLenum target)
{
   const char *vs_source;
   char *fs_source;
   void *mem_ctx;
   enum blit_msaa_shader shader_index;
   bool dst_is_msaa = false;
   GLenum src_datatype;
   const char *vec4_prefix;
   const char *sampler_array_suffix = "";
   char *name;
   const char *texcoord_type = "vec2";
   int samples;
   int shader_offset = 0;

   if (src_rb) {
      samples = MAX2(src_rb->NumSamples, 1);
      src_datatype = _mesa_get_format_datatype(src_rb->Format);
   } else {
      /* depth-or-color glCopyTexImage fallback path that passes a NULL rb and
       * doesn't handle integer.
       */
      samples = 1;
      src_datatype = GL_UNSIGNED_NORMALIZED;
   }

   /* We expect only power of 2 samples in source multisample buffer. */
   assert(samples > 0 && _mesa_is_pow_two(samples));
   while (samples >> (shader_offset + 1)) {
      shader_offset++;
   }
   /* Update the assert if we plan to support more than 16X MSAA. */
   assert(shader_offset >= 0 && shader_offset <= 4);

   if (drawFb->Visual.samples > 1) {
      /* If you're calling meta_BlitFramebuffer with the destination
       * multisampled, this is the only path that will work -- swrast and
       * CopyTexImage won't work on it either.
       */
      assert(ctx->Extensions.ARB_sample_shading);

      dst_is_msaa = true;

      /* We need shader invocation per sample, not per pixel */
      _mesa_set_enable(ctx, GL_MULTISAMPLE, GL_TRUE);
      _mesa_set_enable(ctx, GL_SAMPLE_SHADING, GL_TRUE);
      _mesa_MinSampleShading(1.0);
   }

   switch (target) {
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      if (src_rb && (src_rb->_BaseFormat == GL_DEPTH_COMPONENT ||
          src_rb->_BaseFormat == GL_DEPTH_STENCIL)) {
         if (dst_is_msaa)
            shader_index = BLIT_MSAA_SHADER_2D_MULTISAMPLE_DEPTH_COPY;
         else
            shader_index = BLIT_MSAA_SHADER_2D_MULTISAMPLE_DEPTH_RESOLVE;
      } else {
         if (dst_is_msaa)
            shader_index = BLIT_MSAA_SHADER_2D_MULTISAMPLE_COPY;
         else {
            shader_index = BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE +
                           shader_offset;
         }
      }

      if (target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY) {
         shader_index += (BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_ARRAY_RESOLVE -
                          BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE);
         sampler_array_suffix = "Array";
         texcoord_type = "vec3";
      }
      break;
   default:
      _mesa_problem(ctx, "Unknown texture target %s\n",
                    _mesa_enum_to_string(target));
      shader_index = BLIT_2X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE;
   }

   /* We rely on the enum being sorted this way. */
   STATIC_ASSERT(BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE_INT ==
                 BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE + 5);
   STATIC_ASSERT(BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE_UINT ==
                 BLIT_1X_MSAA_SHADER_2D_MULTISAMPLE_RESOLVE + 10);
   if (src_datatype == GL_INT) {
      shader_index += 5;
      vec4_prefix = "i";
   } else if (src_datatype == GL_UNSIGNED_INT) {
      shader_index += 10;
      vec4_prefix = "u";
   } else {
      vec4_prefix = "";
   }

   if (blit->msaa_shaders[shader_index]) {
      _mesa_UseProgram(blit->msaa_shaders[shader_index]);
      return;
   }

   mem_ctx = ralloc_context(NULL);

   if (shader_index == BLIT_MSAA_SHADER_2D_MULTISAMPLE_DEPTH_RESOLVE ||
       shader_index == BLIT_MSAA_SHADER_2D_MULTISAMPLE_ARRAY_DEPTH_RESOLVE ||
       shader_index == BLIT_MSAA_SHADER_2D_MULTISAMPLE_ARRAY_DEPTH_COPY ||
       shader_index == BLIT_MSAA_SHADER_2D_MULTISAMPLE_DEPTH_COPY) {
      char *sample_index;
      const char *tex_coords = "texCoords";

      if (dst_is_msaa) {
         sample_index = "gl_SampleID";
         name = "depth MSAA copy";

         if (ctx->Extensions.ARB_gpu_shader5 && samples >= 16) {
            /* See comment below for the color copy */
            tex_coords = "interpolateAtOffset(texCoords, vec2(0.0))";
         }
      } else {
         /* From the GL 4.3 spec:
          *
          *     "If there is a multisample buffer (the value of SAMPLE_BUFFERS
          *      is one), then values are obtained from the depth samples in
          *      this buffer. It is recommended that the depth value of the
          *      centermost sample be used, though implementations may choose
          *      any function of the depth sample values at each pixel.
          *
          * We're slacking and instead of choosing centermost, we've got 0.
          */
         sample_index = "0";
         name = "depth MSAA resolve";
      }

      vs_source = ralloc_asprintf(mem_ctx,
                                  "#version 130\n"
                                  "in vec2 position;\n"
                                  "in %s textureCoords;\n"
                                  "out %s texCoords;\n"
                                  "void main()\n"
                                  "{\n"
                                  "   texCoords = textureCoords;\n"
                                  "   gl_Position = vec4(position, 0.0, 1.0);\n"
                                  "}\n",
                                  texcoord_type,
                                  texcoord_type);
      fs_source = ralloc_asprintf(mem_ctx,
                                  "#version 130\n"
                                  "#extension GL_ARB_texture_multisample : enable\n"
                                  "#extension GL_ARB_sample_shading : enable\n"
                                  "#extension GL_ARB_gpu_shader5 : enable\n"
                                  "uniform sampler2DMS%s texSampler;\n"
                                  "in %s texCoords;\n"
                                  "out vec4 out_color;\n"
                                  "\n"
                                  "void main()\n"
                                  "{\n"
                                  "   gl_FragDepth = texelFetch(texSampler, i%s(%s), %s).r;\n"
                                  "}\n",
                                  sampler_array_suffix,
                                  texcoord_type,
                                  texcoord_type,
                                  tex_coords,
                                  sample_index);
   } else {
      /* You can create 2D_MULTISAMPLE textures with 0 sample count (meaning 1
       * sample).  Yes, this is ridiculous.
       */
      char *sample_resolve;
      const char *merge_function;
      name = ralloc_asprintf(mem_ctx, "%svec4 MSAA %s",
                             vec4_prefix,
                             dst_is_msaa ? "copy" : "resolve");

      if (dst_is_msaa) {
         const char *tex_coords;

         if (ctx->Extensions.ARB_gpu_shader5 && samples >= 16) {
            /* If interpolateAtOffset is available then it will be used to
             * force the interpolation to the center. This is required at
             * least on Intel hardware because it is possible to have a sample
             * position on the 0 x or y axis which means it will lie exactly
             * on the pixel boundary. If we let the hardware interpolate the
             * coordinates at one of these positions then it is possible for
             * it to jump to a neighboring texel when converting to ints due
             * to rounding errors. This is only done for >= 16x MSAA because
             * it probably has some overhead. It is more likely that some
             * hardware will use one of these problematic positions at 16x
             * MSAA because in that case in D3D they are defined to be at
             * these positions.
             */
            tex_coords = "interpolateAtOffset(texCoords, vec2(0.0))";
         } else {
            tex_coords = "texCoords";
         }

         sample_resolve =
            ralloc_asprintf(mem_ctx,
                            "   out_color = texelFetch(texSampler, "
                            "i%s(%s), gl_SampleID);",
                            texcoord_type, tex_coords);

         merge_function = "";
      } else {
         int i;
         int step;

         if (src_datatype == GL_INT || src_datatype == GL_UNSIGNED_INT) {
            merge_function =
               "gvec4 merge(gvec4 a, gvec4 b) { return (a >> gvec4(1)) + (b >> gvec4(1)) + (a & b & gvec4(1)); }\n";
         } else {
            /* The divide will happen at the end for floats. */
            merge_function =
               "vec4 merge(vec4 a, vec4 b) { return (a + b); }\n";
         }

         /* We're assuming power of two samples for this resolution procedure.
          *
          * To avoid losing any floating point precision if the samples all
          * happen to have the same value, we merge pairs of values at a time
          * (so the floating point exponent just gets increased), rather than
          * doing a naive sum and dividing.
          */
         assert(_mesa_is_pow_two(samples));
         /* Fetch each individual sample. */
         sample_resolve = rzalloc_size(mem_ctx, 1);
         for (i = 0; i < samples; i++) {
            ralloc_asprintf_append(&sample_resolve,
                                   "   gvec4 sample_1_%d = texelFetch(texSampler, i%s(texCoords), %d);\n",
                                   i, texcoord_type, i);
         }
         /* Now, merge each pair of samples, then merge each pair of those,
          * etc.
          */
         for (step = 2; step <= samples; step *= 2) {
            for (i = 0; i < samples; i += step) {
               ralloc_asprintf_append(&sample_resolve,
                                      "   gvec4 sample_%d_%d = merge(sample_%d_%d, sample_%d_%d);\n",
                                      step, i,
                                      step / 2, i,
                                      step / 2, i + step / 2);
            }
         }

         /* Scale the final result. */
         if (src_datatype == GL_UNSIGNED_INT || src_datatype == GL_INT) {
            ralloc_asprintf_append(&sample_resolve,
                                   "   out_color = sample_%d_0;\n",
                                   samples);
         } else {
            ralloc_asprintf_append(&sample_resolve,
                                   "   gl_FragColor = sample_%d_0 / %f;\n",
                                   samples, (float)samples);
         }
      }

      vs_source = ralloc_asprintf(mem_ctx,
                                  "#version 130\n"
                                  "in vec2 position;\n"
                                  "in %s textureCoords;\n"
                                  "out %s texCoords;\n"
                                  "void main()\n"
                                  "{\n"
                                  "   texCoords = textureCoords;\n"
                                  "   gl_Position = vec4(position, 0.0, 1.0);\n"
                                  "}\n",
                                  texcoord_type,
                                  texcoord_type);
      fs_source = ralloc_asprintf(mem_ctx,
                                  "#version 130\n"
                                  "#extension GL_ARB_texture_multisample : enable\n"
                                  "#extension GL_ARB_sample_shading : enable\n"
                                  "#extension GL_ARB_gpu_shader5 : enable\n"
                                  "#define gvec4 %svec4\n"
                                  "uniform %ssampler2DMS%s texSampler;\n"
                                  "in %s texCoords;\n"
                                  "out gvec4 out_color;\n"
                                  "\n"
                                  "%s" /* merge_function */
                                  "void main()\n"
                                  "{\n"
                                  "%s\n" /* sample_resolve */
                                  "}\n",
                                  vec4_prefix,
                                  vec4_prefix,
                                  sampler_array_suffix,
                                  texcoord_type,
                                  merge_function,
                                  sample_resolve);
   }

   _mesa_meta_compile_and_link_program(ctx, vs_source, fs_source, name,
                                       &blit->msaa_shaders[shader_index]);

   ralloc_free(mem_ctx);
}

static void
setup_glsl_blit_framebuffer(struct gl_context *ctx,
                            struct blit_state *blit,
                            const struct gl_framebuffer *drawFb,
                            struct gl_renderbuffer *src_rb,
                            GLenum target, GLenum filter,
                            bool is_scaled_blit,
                            bool do_depth)
{
   unsigned texcoord_size;
   bool is_target_multisample = target == GL_TEXTURE_2D_MULTISAMPLE ||
                                target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY;
   bool is_filter_scaled_resolve = filter == GL_SCALED_RESOLVE_FASTEST_EXT ||
                                   filter == GL_SCALED_RESOLVE_NICEST_EXT;

   /* target = GL_TEXTURE_RECTANGLE is not supported in GLES 3.0 */
   assert(_mesa_is_desktop_gl(ctx) || target == GL_TEXTURE_2D);

   texcoord_size = 2 + (src_rb->Depth > 1 ? 1 : 0);

   _mesa_meta_setup_vertex_objects(ctx, &blit->VAO, &blit->buf_obj, true,
                                   2, texcoord_size, 0);

   if (is_target_multisample && is_filter_scaled_resolve && is_scaled_blit) {
      setup_glsl_msaa_blit_scaled_shader(ctx, blit, src_rb, target, filter);
   } else if (is_target_multisample) {
      setup_glsl_msaa_blit_shader(ctx, blit, drawFb, src_rb, target);
   } else {
      _mesa_meta_setup_blit_shader(ctx, target, do_depth,
                                   do_depth ? &blit->shaders_with_depth
                                            : &blit->shaders_without_depth);
   }
}

/**
 * Try to do a color or depth glBlitFramebuffer using texturing.
 *
 * We can do this when the src renderbuffer is actually a texture, or when the
 * driver exposes BindRenderbufferTexImage().
 */
static bool
blitframebuffer_texture(struct gl_context *ctx,
                        const struct gl_framebuffer *readFb,
                        const struct gl_framebuffer *drawFb,
                        GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                        GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                        GLenum filter, GLint flipX, GLint flipY,
                        GLboolean glsl_version, GLboolean do_depth)
{
   int att_index = do_depth ? BUFFER_DEPTH : readFb->_ColorReadBufferIndex;
   const struct gl_renderbuffer_attachment *readAtt =
      &readFb->Attachment[att_index];
   struct blit_state *blit = &ctx->Meta->Blit;
   struct fb_tex_blit_state fb_tex_blit;
   const GLint dstX = MIN2(dstX0, dstX1);
   const GLint dstY = MIN2(dstY0, dstY1);
   const GLint dstW = abs(dstX1 - dstX0);
   const GLint dstH = abs(dstY1 - dstY0);
   const int srcW = abs(srcX1 - srcX0);
   const int srcH = abs(srcY1 - srcY0);
   bool scaled_blit = false;
   struct gl_texture_object *texObj;
   GLuint srcLevel;
   GLenum target;
   struct gl_renderbuffer *rb = readAtt->Renderbuffer;
   struct temp_texture *meta_temp_texture;

   if (rb->NumSamples && !ctx->Extensions.ARB_texture_multisample)
      return false;

   _mesa_meta_fb_tex_blit_begin(ctx, &fb_tex_blit);

   if (readAtt->Texture &&
       (readAtt->Texture->Target == GL_TEXTURE_2D ||
        readAtt->Texture->Target == GL_TEXTURE_RECTANGLE ||
        readAtt->Texture->Target == GL_TEXTURE_2D_MULTISAMPLE ||
        readAtt->Texture->Target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)) {
      /* If there's a texture attached of a type we can handle, then just use
       * it directly.
       */
      srcLevel = readAtt->TextureLevel;
      texObj = readAtt->Texture;
      target = texObj->Target;
   } else if (!readAtt->Texture && ctx->Driver.BindRenderbufferTexImage) {
      if (!_mesa_meta_bind_rb_as_tex_image(ctx, rb, &fb_tex_blit.tempTex,
                                           &texObj, &target))
         return false;

      srcLevel = 0;
      if (_mesa_is_winsys_fbo(readFb)) {
         GLint temp = srcY0;
         srcY0 = rb->Height - srcY1;
         srcY1 = rb->Height - temp;
         flipY = -flipY;
      }
   } else {
      GLenum tex_base_format;
      /* Fall back to doing a CopyTexSubImage to get the destination
       * renderbuffer into a texture.
       */
      if (ctx->Meta->Blit.no_ctsi_fallback)
         return false;

      if (rb->NumSamples > 1)
         return false;

      if (do_depth) {
         meta_temp_texture = _mesa_meta_get_temp_depth_texture(ctx);
         tex_base_format = GL_DEPTH_COMPONENT;
      } else {
         meta_temp_texture = _mesa_meta_get_temp_texture(ctx);
         tex_base_format =
            _mesa_base_tex_format(ctx, rb->InternalFormat);
      }

      srcLevel = 0;
      target = meta_temp_texture->Target;
      texObj = _mesa_lookup_texture(ctx, meta_temp_texture->TexObj);
      if (texObj == NULL) {
         return false;
      }

      _mesa_meta_setup_copypix_texture(ctx, meta_temp_texture,
                                       srcX0, srcY0,
                                       srcW, srcH,
                                       tex_base_format,
                                       filter);


      srcX0 = 0;
      srcY0 = 0;
      srcX1 = srcW;
      srcY1 = srcH;
   }

   fb_tex_blit.baseLevelSave = texObj->BaseLevel;
   fb_tex_blit.maxLevelSave = texObj->MaxLevel;
   fb_tex_blit.stencilSamplingSave = texObj->StencilSampling;

   scaled_blit = dstW != srcW || dstH != srcH;

   if (glsl_version) {
      setup_glsl_blit_framebuffer(ctx, blit, drawFb, rb, target, filter, scaled_blit,
                                  do_depth);
   }
   else {
      _mesa_meta_setup_ff_tnl_for_blit(ctx,
                                       &ctx->Meta->Blit.VAO,
                                       &ctx->Meta->Blit.buf_obj,
                                       2);
   }

   /*
     printf("Blit from texture!\n");
     printf("  srcAtt %p  dstAtt %p\n", readAtt, drawAtt);
     printf("  srcTex %p  dstText %p\n", texObj, drawAtt->Texture);
   */

   fb_tex_blit.samp_obj = _mesa_meta_setup_sampler(ctx, texObj, target, filter,
                                                   srcLevel);

   /* Always do our blits with no net sRGB decode or encode.
    *
    * However, if both the src and dst can be srgb decode/encoded, enable them
    * so that we do any blending (from scaling or from MSAA resolves) in the
    * right colorspace.
    *
    * Our choice of not doing any net encode/decode is from the GL 3.0
    * specification:
    *
    *     "Blit operations bypass the fragment pipeline. The only fragment
    *      operations which affect a blit are the pixel ownership test and the
    *      scissor test."
    *
    * The GL 4.4 specification disagrees and says that the sRGB part of the
    * fragment pipeline applies, but this was found to break applications.
    */
   if (ctx->Extensions.EXT_texture_sRGB_decode) {
      if (_mesa_get_format_color_encoding(rb->Format) == GL_SRGB &&
          drawFb->Visual.sRGBCapable) {
         _mesa_set_sampler_srgb_decode(ctx, fb_tex_blit.samp_obj,
                                       GL_DECODE_EXT);
         _mesa_set_framebuffer_srgb(ctx, GL_TRUE);
      } else {
         _mesa_set_sampler_srgb_decode(ctx, fb_tex_blit.samp_obj,
                                       GL_SKIP_DECODE_EXT);
         /* set_framebuffer_srgb was set by _mesa_meta_begin(). */
      }
   }

   if (!glsl_version) {
      _mesa_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      _mesa_set_enable(ctx, target, GL_TRUE);
   }

   /* Prepare vertex data (the VBO was previously created and bound) */
   {
      struct vertex verts[4];
      GLfloat s0, t0, s1, t1;

      if (target == GL_TEXTURE_2D) {
         const struct gl_texture_image *texImage
            = _mesa_select_tex_image(texObj, target, srcLevel);
         s0 = srcX0 / (float) texImage->Width;
         s1 = srcX1 / (float) texImage->Width;
         t0 = srcY0 / (float) texImage->Height;
         t1 = srcY1 / (float) texImage->Height;
      }
      else {
         assert(target == GL_TEXTURE_RECTANGLE_ARB ||
                target == GL_TEXTURE_2D_MULTISAMPLE ||
                target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY);
         s0 = (float) srcX0;
         s1 = (float) srcX1;
         t0 = (float) srcY0;
         t1 = (float) srcY1;
      }

      /* Silence valgrind warnings about reading uninitialized stack. */
      memset(verts, 0, sizeof(verts));

      /* setup vertex positions */
      verts[0].x = -1.0F * flipX;
      verts[0].y = -1.0F * flipY;
      verts[1].x =  1.0F * flipX;
      verts[1].y = -1.0F * flipY;
      verts[2].x =  1.0F * flipX;
      verts[2].y =  1.0F * flipY;
      verts[3].x = -1.0F * flipX;
      verts[3].y =  1.0F * flipY;

      verts[0].tex[0] = s0;
      verts[0].tex[1] = t0;
      verts[0].tex[2] = readAtt->Zoffset;
      verts[1].tex[0] = s1;
      verts[1].tex[1] = t0;
      verts[1].tex[2] = readAtt->Zoffset;
      verts[2].tex[0] = s1;
      verts[2].tex[1] = t1;
      verts[2].tex[2] = readAtt->Zoffset;
      verts[3].tex[0] = s0;
      verts[3].tex[1] = t1;
      verts[3].tex[2] = readAtt->Zoffset;

      _mesa_buffer_sub_data(ctx, blit->buf_obj, 0, sizeof(verts), verts,
                            __func__);
   }

   /* setup viewport */
   _mesa_set_viewport(ctx, 0, dstX, dstY, dstW, dstH);
   _mesa_ColorMask(!do_depth, !do_depth, !do_depth, !do_depth);
   _mesa_set_enable(ctx, GL_DEPTH_TEST, do_depth);
   _mesa_DepthMask(do_depth);
   _mesa_DepthFunc(GL_ALWAYS);

   _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
   _mesa_meta_fb_tex_blit_end(ctx, target, &fb_tex_blit);

   return true;
}

void
_mesa_meta_fb_tex_blit_begin(struct gl_context *ctx,
                             struct fb_tex_blit_state *blit)
{
   /* None of the existing callers preinitialize fb_tex_blit_state to zeros,
    * and both use stack variables.  If samp_obj_save is not NULL,
    * _mesa_reference_sampler_object will try to dereference it.  Leaving
    * random garbage in samp_obj_save can only lead to crashes.
    *
    * Since the state isn't persistent across calls, we won't catch ref
    * counting problems.
    */
   blit->samp_obj_save = NULL;
   _mesa_reference_sampler_object(ctx, &blit->samp_obj_save,
                                  ctx->Texture.Unit[ctx->Texture.CurrentUnit].Sampler);
   blit->tempTex = 0;
}

void
_mesa_meta_fb_tex_blit_end(struct gl_context *ctx, GLenum target,
                           struct fb_tex_blit_state *blit)
{
   struct gl_texture_object *const texObj =
      _mesa_get_current_tex_object(ctx, target);

   /* Restore texture object state, the texture binding will
    * be restored by _mesa_meta_end().
    */
   if (target != GL_TEXTURE_RECTANGLE_ARB) {
      _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_BASE_LEVEL,
                                &blit->baseLevelSave, false);
      _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_MAX_LEVEL,
                                &blit->maxLevelSave, false);
   }

   /* If ARB_stencil_texturing is not supported, the mode won't have changed. */
   if (texObj->StencilSampling != blit->stencilSamplingSave) {
      /* GLint so the compiler won't complain about type signedness mismatch
       * in the call to _mesa_texture_parameteriv below.
       */
      const GLint param = blit->stencilSamplingSave ?
         GL_STENCIL_INDEX : GL_DEPTH_COMPONENT;

      _mesa_texture_parameteriv(ctx, texObj, GL_DEPTH_STENCIL_TEXTURE_MODE,
                                &param, false);
   }

   _mesa_bind_sampler(ctx, ctx->Texture.CurrentUnit, blit->samp_obj_save);
   _mesa_reference_sampler_object(ctx, &blit->samp_obj_save, NULL);
   _mesa_reference_sampler_object(ctx, &blit->samp_obj, NULL);

   if (blit->tempTex)
      _mesa_DeleteTextures(1, &blit->tempTex);
}

GLboolean
_mesa_meta_bind_rb_as_tex_image(struct gl_context *ctx,
                                struct gl_renderbuffer *rb,
                                GLuint *tex,
                                struct gl_texture_object **texObj,
                                GLenum *target)
{
   struct gl_texture_image *texImage;
   GLuint tempTex;

   if (rb->NumSamples > 1)
      *target = GL_TEXTURE_2D_MULTISAMPLE;
   else
      *target = GL_TEXTURE_2D;

   tempTex = 0;
   _mesa_GenTextures(1, &tempTex);
   if (tempTex == 0)
      return false;

   *tex = tempTex;

   _mesa_BindTexture(*target, *tex);
   *texObj = _mesa_lookup_texture(ctx, *tex);
   texImage = _mesa_get_tex_image(ctx, *texObj, *target, 0);

   if (!ctx->Driver.BindRenderbufferTexImage(ctx, rb, texImage)) {
      _mesa_DeleteTextures(1, tex);
      return false;
   }

   if (ctx->Driver.FinishRenderTexture && !rb->NeedsFinishRenderTexture) {
      rb->NeedsFinishRenderTexture = true;
      ctx->Driver.FinishRenderTexture(ctx, rb);
   }

   return true;
}

struct gl_sampler_object *
_mesa_meta_setup_sampler(struct gl_context *ctx,
                         struct gl_texture_object *texObj,
                         GLenum target, GLenum filter, GLuint srcLevel)
{
   struct gl_sampler_object *samp_obj;
   GLenum tex_filter = (filter == GL_SCALED_RESOLVE_FASTEST_EXT ||
                        filter == GL_SCALED_RESOLVE_NICEST_EXT) ?
                       GL_NEAREST : filter;

   samp_obj =  ctx->Driver.NewSamplerObject(ctx, 0xDEADBEEF);
   if (samp_obj == NULL)
      return NULL;

   _mesa_bind_sampler(ctx, ctx->Texture.CurrentUnit, samp_obj);
   _mesa_set_sampler_filters(ctx, samp_obj, tex_filter, tex_filter);
   _mesa_set_sampler_wrap(ctx, samp_obj, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
                          samp_obj->WrapR);

   /* Prepare src texture state */
   _mesa_BindTexture(target, texObj->Name);
   if (target != GL_TEXTURE_RECTANGLE_ARB) {
      _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_BASE_LEVEL,
                                (GLint *) &srcLevel, false);
      _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_MAX_LEVEL,
                                (GLint *) &srcLevel, false);
   }

   return samp_obj;
}

/**
 * Meta implementation of ctx->Driver.BlitFramebuffer() in terms
 * of texture mapping and polygon rendering.
 */
GLbitfield
_mesa_meta_BlitFramebuffer(struct gl_context *ctx,
                           const struct gl_framebuffer *readFb,
                           const struct gl_framebuffer *drawFb,
                           GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                           GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                           GLbitfield mask, GLenum filter)
{
   const GLint dstW = abs(dstX1 - dstX0);
   const GLint dstH = abs(dstY1 - dstY0);
   const GLint dstFlipX = (dstX1 - dstX0) / dstW;
   const GLint dstFlipY = (dstY1 - dstY0) / dstH;

   struct {
      GLint srcX0, srcY0, srcX1, srcY1;
      GLint dstX0, dstY0, dstX1, dstY1;
   } clip = {
      srcX0, srcY0, srcX1, srcY1,
      dstX0, dstY0, dstX1, dstY1
   };

   const GLboolean use_glsl_version = ctx->Extensions.ARB_vertex_shader &&
                                      ctx->Extensions.ARB_fragment_shader;

   /* Multisample texture blit support requires texture multisample. */
   if (readFb->Visual.samples > 0 &&
       !ctx->Extensions.ARB_texture_multisample) {
      return mask;
   }

   /* Clip a copy of the blit coordinates. If these differ from the input
    * coordinates, then we'll set the scissor.
    */
   if (!_mesa_clip_blit(ctx, readFb, drawFb,
                        &clip.srcX0, &clip.srcY0, &clip.srcX1, &clip.srcY1,
                        &clip.dstX0, &clip.dstY0, &clip.dstX1, &clip.dstY1)) {
      /* clipped/scissored everything away */
      return 0;
   }

   /* Only scissor affects blit, but we're doing to set a custom scissor if
    * necessary anyway, so save/clear state.
    */
   _mesa_meta_begin(ctx, MESA_META_ALL & ~MESA_META_DRAW_BUFFERS);

   /* Dithering shouldn't be performed for glBlitFramebuffer */
   _mesa_set_enable(ctx, GL_DITHER, GL_FALSE);

   /* If the clipping earlier changed the destination rect at all, then
    * enable the scissor to clip to it.
    */
   if (clip.dstX0 != dstX0 || clip.dstY0 != dstY0 ||
       clip.dstX1 != dstX1 || clip.dstY1 != dstY1) {
      _mesa_set_enable(ctx, GL_SCISSOR_TEST, GL_TRUE);
      _mesa_Scissor(MIN2(clip.dstX0, clip.dstX1),
                    MIN2(clip.dstY0, clip.dstY1),
                    abs(clip.dstX0 - clip.dstX1),
                    abs(clip.dstY0 - clip.dstY1));
   }

   /* Try faster, direct texture approach first */
   if (mask & GL_COLOR_BUFFER_BIT) {
      if (blitframebuffer_texture(ctx, readFb, drawFb,
                                  srcX0, srcY0, srcX1, srcY1,
                                  dstX0, dstY0, dstX1, dstY1,
                                  filter, dstFlipX, dstFlipY,
                                  use_glsl_version, false)) {
         mask &= ~GL_COLOR_BUFFER_BIT;
      }
   }

   if (mask & GL_DEPTH_BUFFER_BIT && use_glsl_version) {
      if (blitframebuffer_texture(ctx, readFb, drawFb,
                                  srcX0, srcY0, srcX1, srcY1,
                                  dstX0, dstY0, dstX1, dstY1,
                                  filter, dstFlipX, dstFlipY,
                                  use_glsl_version, true)) {
         mask &= ~GL_DEPTH_BUFFER_BIT;
      }
   }

   if (mask & GL_STENCIL_BUFFER_BIT) {
      /* XXX can't easily do stencil */
   }

   _mesa_meta_end(ctx);

   return mask;
}

void
_mesa_meta_glsl_blit_cleanup(struct gl_context *ctx, struct blit_state *blit)
{
   if (blit->VAO) {
      _mesa_DeleteVertexArrays(1, &blit->VAO);
      blit->VAO = 0;
      _mesa_reference_buffer_object(ctx, &blit->buf_obj, NULL);
   }

   _mesa_meta_blit_shader_table_cleanup(&blit->shaders_with_depth);
   _mesa_meta_blit_shader_table_cleanup(&blit->shaders_without_depth);

   _mesa_DeleteTextures(1, &blit->depthTex.TexObj);
   blit->depthTex.TexObj = 0;
}

void
_mesa_meta_and_swrast_BlitFramebuffer(struct gl_context *ctx,
                                      struct gl_framebuffer *readFb,
                                      struct gl_framebuffer *drawFb,
                                      GLint srcX0, GLint srcY0,
                                      GLint srcX1, GLint srcY1,
                                      GLint dstX0, GLint dstY0,
                                      GLint dstX1, GLint dstY1,
                                      GLbitfield mask, GLenum filter)
{
   mask = _mesa_meta_BlitFramebuffer(ctx, readFb, drawFb,
                                     srcX0, srcY0, srcX1, srcY1,
                                     dstX0, dstY0, dstX1, dstY1,
                                     mask, filter);
   if (mask == 0x0)
      return;

   _swrast_BlitFramebuffer(ctx, readFb, drawFb,
                           srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1,
                           mask, filter);
}
