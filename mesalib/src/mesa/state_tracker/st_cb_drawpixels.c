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

 /*
  * Authors:
  *   Brian Paul
  */

#include "main/errors.h"
#include "main/imports.h"
#include "main/image.h"
#include "main/bufferobj.h"
#include "main/blit.h"
#include "main/format_pack.h"
#include "main/framebuffer.h"
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/pack.h"
#include "main/pbo.h"
#include "main/readpix.h"
#include "main/state.h"
#include "main/texformat.h"
#include "main/teximage.h"
#include "main/texstore.h"
#include "main/glformats.h"
#include "program/program.h"
#include "program/prog_print.h"
#include "program/prog_instruction.h"

#include "st_atom.h"
#include "st_atom_constbuf.h"
#include "st_cb_bitmap.h"
#include "st_cb_drawpixels.h"
#include "st_cb_readpixels.h"
#include "st_cb_fbo.h"
#include "st_context.h"
#include "st_debug.h"
#include "st_draw.h"
#include "st_format.h"
#include "st_program.h"
#include "st_sampler_view.h"
#include "st_scissor.h"
#include "st_texture.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "tgsi/tgsi_ureg.h"
#include "util/u_format.h"
#include "util/u_inlines.h"
#include "util/u_math.h"
#include "util/u_tile.h"
#include "cso_cache/cso_context.h"


/**
 * We have a simple glDrawPixels cache to try to optimize the case where the
 * same image is drawn over and over again.  It basically works as follows:
 *
 * 1. After we construct a texture map with the image and draw it, we do
 *    not discard the texture.  We keep it around, plus we note the
 *    glDrawPixels width, height, format, etc. parameters and keep a copy
 *    of the image in a malloc'd buffer.
 *
 * 2. On the next glDrawPixels we check if the parameters match the previous
 *    call.  If those match, we check if the image matches the previous image
 *    via a memcmp() call.  If everything matches, we re-use the previous
 *    texture, thereby avoiding the cost creating a new texture and copying
 *    the image to it.
 *
 * The effectiveness of this cache depends upon:
 * 1. If the memcmp() finds a difference, it happens relatively quickly.
      Hopefully, not just the last pixels differ!
 * 2. If the memcmp() finds no difference, doing that check is faster than
 *    creating and loading a texture.
 *
 * Notes:
 * 1. We don't support any pixel unpacking parameters.
 * 2. We don't try to cache images in Pixel Buffer Objects.
 * 3. Instead of saving the whole image, perhaps some sort of reliable
 *    checksum function could be used instead.
 */
#define USE_DRAWPIXELS_CACHE 1



/**
 * Create fragment program that does a TEX() instruction to get a Z and/or
 * stencil value value, then writes to FRAG_RESULT_DEPTH/FRAG_RESULT_STENCIL.
 * Used for glDrawPixels(GL_DEPTH_COMPONENT / GL_STENCIL_INDEX).
 * Pass fragment color through as-is.
 *
 * \return CSO of the fragment shader.
 */
static void *
get_drawpix_z_stencil_program(struct st_context *st,
                              GLboolean write_depth,
                              GLboolean write_stencil)
{
   struct ureg_program *ureg;
   struct ureg_src depth_sampler, stencil_sampler;
   struct ureg_src texcoord, color;
   struct ureg_dst out_color, out_depth, out_stencil;
   const GLuint shaderIndex = write_depth * 2 + write_stencil;
   void *cso;

   assert(shaderIndex < ARRAY_SIZE(st->drawpix.zs_shaders));

   if (st->drawpix.zs_shaders[shaderIndex]) {
      /* already have the proper shader */
      return st->drawpix.zs_shaders[shaderIndex];
   }

   ureg = ureg_create(PIPE_SHADER_FRAGMENT);
   if (ureg == NULL)
      return NULL;

   ureg_property(ureg, TGSI_PROPERTY_FS_COLOR0_WRITES_ALL_CBUFS, TRUE);

   if (write_depth) {
      color = ureg_DECL_fs_input(ureg, TGSI_SEMANTIC_COLOR, 0,
                                 TGSI_INTERPOLATE_COLOR);
      out_color = ureg_DECL_output(ureg, TGSI_SEMANTIC_COLOR, 0);

      depth_sampler = ureg_DECL_sampler(ureg, 0);
      ureg_DECL_sampler_view(ureg, 0, TGSI_TEXTURE_2D,
                             TGSI_RETURN_TYPE_FLOAT,
                             TGSI_RETURN_TYPE_FLOAT,
                             TGSI_RETURN_TYPE_FLOAT,
                             TGSI_RETURN_TYPE_FLOAT);
      out_depth = ureg_DECL_output(ureg, TGSI_SEMANTIC_POSITION, 0);
   }

   if (write_stencil) {
      stencil_sampler = ureg_DECL_sampler(ureg, 1);
      ureg_DECL_sampler_view(ureg, 1, TGSI_TEXTURE_2D,
                             TGSI_RETURN_TYPE_UINT,
                             TGSI_RETURN_TYPE_UINT,
                             TGSI_RETURN_TYPE_UINT,
                             TGSI_RETURN_TYPE_UINT);
      out_stencil = ureg_DECL_output(ureg, TGSI_SEMANTIC_STENCIL, 0);
   }

   texcoord = ureg_DECL_fs_input(ureg,
                                 st->needs_texcoord_semantic ?
                                    TGSI_SEMANTIC_TEXCOORD :
                                    TGSI_SEMANTIC_GENERIC,
                                 0, TGSI_INTERPOLATE_LINEAR);

   if (write_depth) {
      ureg_TEX(ureg, ureg_writemask(out_depth, TGSI_WRITEMASK_Z),
               TGSI_TEXTURE_2D, texcoord, depth_sampler);
      ureg_MOV(ureg, out_color, color);
   }

   if (write_stencil)
      ureg_TEX(ureg, ureg_writemask(out_stencil, TGSI_WRITEMASK_Y),
               TGSI_TEXTURE_2D, texcoord, stencil_sampler);

   ureg_END(ureg);
   cso = ureg_create_shader_and_destroy(ureg, st->pipe);

   /* save the new shader */
   st->drawpix.zs_shaders[shaderIndex] = cso;
   return cso;
}


/**
 * Create a simple vertex shader that just passes through the
 * vertex position and texcoord (and optionally, color).
 */
static void *
make_passthrough_vertex_shader(struct st_context *st, 
                               GLboolean passColor)
{
   const enum tgsi_semantic texcoord_semantic = st->needs_texcoord_semantic ?
      TGSI_SEMANTIC_TEXCOORD : TGSI_SEMANTIC_GENERIC;

   if (!st->drawpix.vert_shaders[passColor]) {
      struct ureg_program *ureg = ureg_create( PIPE_SHADER_VERTEX );

      if (ureg == NULL)
         return NULL;

      /* MOV result.pos, vertex.pos; */
      ureg_MOV(ureg,
               ureg_DECL_output( ureg, TGSI_SEMANTIC_POSITION, 0 ),
               ureg_DECL_vs_input( ureg, 0 ));

      if (passColor) {
         /* MOV result.color0, vertex.attr[1]; */
         ureg_MOV(ureg,
                  ureg_DECL_output( ureg, TGSI_SEMANTIC_COLOR, 0 ),
                  ureg_DECL_vs_input( ureg, 1 ));
      }

      /* MOV result.texcoord0, vertex.attr[2]; */
      ureg_MOV(ureg,
               ureg_DECL_output( ureg, texcoord_semantic, 0 ),
               ureg_DECL_vs_input( ureg, 2 ));

      ureg_END( ureg );
      
      st->drawpix.vert_shaders[passColor] = 
         ureg_create_shader_and_destroy( ureg, st->pipe );
   }

   return st->drawpix.vert_shaders[passColor];
}


/**
 * Return a texture internalFormat for drawing/copying an image
 * of the given format and type.
 */
static GLenum
internal_format(struct gl_context *ctx, GLenum format, GLenum type)
{
   switch (format) {
   case GL_DEPTH_COMPONENT:
      switch (type) {
      case GL_UNSIGNED_SHORT:
         return GL_DEPTH_COMPONENT16;

      case GL_UNSIGNED_INT:
         return GL_DEPTH_COMPONENT32;

      case GL_FLOAT:
         if (ctx->Extensions.ARB_depth_buffer_float)
            return GL_DEPTH_COMPONENT32F;
         else
            return GL_DEPTH_COMPONENT;

      default:
         return GL_DEPTH_COMPONENT;
      }

   case GL_DEPTH_STENCIL:
      switch (type) {
      case GL_FLOAT_32_UNSIGNED_INT_24_8_REV:
         return GL_DEPTH32F_STENCIL8;

      case GL_UNSIGNED_INT_24_8:
      default:
         return GL_DEPTH24_STENCIL8;
      }

   case GL_STENCIL_INDEX:
      return GL_STENCIL_INDEX;

   default:
      if (_mesa_is_enum_format_integer(format)) {
         switch (type) {
         case GL_BYTE:
            return GL_RGBA8I;
         case GL_UNSIGNED_BYTE:
            return GL_RGBA8UI;
         case GL_SHORT:
            return GL_RGBA16I;
         case GL_UNSIGNED_SHORT:
            return GL_RGBA16UI;
         case GL_INT:
            return GL_RGBA32I;
         case GL_UNSIGNED_INT:
            return GL_RGBA32UI;
         default:
            assert(0 && "Unexpected type in internal_format()");
            return GL_RGBA_INTEGER;
         }
      }
      else {
         switch (type) {
         case GL_UNSIGNED_BYTE:
         case GL_UNSIGNED_INT_8_8_8_8:
         case GL_UNSIGNED_INT_8_8_8_8_REV:
         default:
            return GL_RGBA8;

         case GL_UNSIGNED_BYTE_3_3_2:
         case GL_UNSIGNED_BYTE_2_3_3_REV:
            return GL_R3_G3_B2;

         case GL_UNSIGNED_SHORT_4_4_4_4:
         case GL_UNSIGNED_SHORT_4_4_4_4_REV:
            return GL_RGBA4;

         case GL_UNSIGNED_SHORT_5_6_5:
         case GL_UNSIGNED_SHORT_5_6_5_REV:
            return GL_RGB565;

         case GL_UNSIGNED_SHORT_5_5_5_1:
         case GL_UNSIGNED_SHORT_1_5_5_5_REV:
            return GL_RGB5_A1;

         case GL_UNSIGNED_INT_10_10_10_2:
         case GL_UNSIGNED_INT_2_10_10_10_REV:
            return GL_RGB10_A2;

         case GL_UNSIGNED_SHORT:
         case GL_UNSIGNED_INT:
            return GL_RGBA16;

         case GL_BYTE:
            return
               ctx->Extensions.EXT_texture_snorm ? GL_RGBA8_SNORM : GL_RGBA8;

         case GL_SHORT:
         case GL_INT:
            return
               ctx->Extensions.EXT_texture_snorm ? GL_RGBA16_SNORM : GL_RGBA16;

         case GL_HALF_FLOAT_ARB:
            return
               ctx->Extensions.ARB_texture_float ? GL_RGBA16F :
               ctx->Extensions.EXT_texture_snorm ? GL_RGBA16_SNORM : GL_RGBA16;

         case GL_FLOAT:
         case GL_DOUBLE:
            return
               ctx->Extensions.ARB_texture_float ? GL_RGBA32F :
               ctx->Extensions.EXT_texture_snorm ? GL_RGBA16_SNORM : GL_RGBA16;

         case GL_UNSIGNED_INT_5_9_9_9_REV:
            assert(ctx->Extensions.EXT_texture_shared_exponent);
            return GL_RGB9_E5;

         case GL_UNSIGNED_INT_10F_11F_11F_REV:
            assert(ctx->Extensions.EXT_packed_float);
            return GL_R11F_G11F_B10F;
         }
      }
   }
}


/**
 * Create a temporary texture to hold an image of the given size.
 * If width, height are not POT and the driver only handles POT textures,
 * allocate the next larger size of texture that is POT.
 */
static struct pipe_resource *
alloc_texture(struct st_context *st, GLsizei width, GLsizei height,
              enum pipe_format texFormat, unsigned bind)
{
   struct pipe_resource *pt;

   pt = st_texture_create(st, st->internal_target, texFormat, 0,
                          width, height, 1, 1, 0, bind);

   return pt;
}


/**
 * Search the cache for an image which matches the given parameters.
 * \return  pipe_resource pointer if found, NULL if not found.
 */
static struct pipe_resource *
search_drawpixels_cache(struct st_context *st,
                        GLsizei width, GLsizei height,
                        GLenum format, GLenum type,
                        const struct gl_pixelstore_attrib *unpack,
                        const void *pixels)
{
   struct pipe_resource *pt = NULL;
   const GLint bpp = _mesa_bytes_per_pixel(format, type);
   unsigned i;

   if ((unpack->RowLength != 0 && unpack->RowLength != width) ||
       unpack->SkipPixels != 0 ||
       unpack->SkipRows != 0 ||
       unpack->SwapBytes ||
       _mesa_is_bufferobj(unpack->BufferObj)) {
      /* we don't allow non-default pixel unpacking values */
      return NULL;
   }

   /* Search cache entries for a match */
   for (i = 0; i < ARRAY_SIZE(st->drawpix_cache.entries); i++) {
      struct drawpix_cache_entry *entry = &st->drawpix_cache.entries[i];

      if (width == entry->width &&
          height == entry->height &&
          format == entry->format &&
          type == entry->type &&
          pixels == entry->user_pointer &&
          entry->image) {
         assert(entry->texture);

         /* check if the pixel data is the same */
         if (memcmp(pixels, entry->image, width * height * bpp) == 0) {
            /* Success - found a cache match */
            pipe_resource_reference(&pt, entry->texture);
            /* refcount of returned texture should be at least two here.  One
             * reference for the cache to hold on to, one for the caller (which
             * it will release), and possibly more held by the driver.
             */
            assert(pt->reference.count >= 2);

            /* update the age of this entry */
            entry->age = ++st->drawpix_cache.age;

            return pt;
         }
      }
   }

   /* no cache match found */
   return NULL;
}


/**
 * Find the oldest entry in the glDrawPixels cache.  We'll replace this
 * one when we need to store a new image.
 */
static struct drawpix_cache_entry *
find_oldest_drawpixels_cache_entry(struct st_context *st)
{
   unsigned oldest_age = ~0u, oldest_index = ~0u;
   unsigned i;

   /* Find entry with oldest (lowest) age */
   for (i = 0; i < ARRAY_SIZE(st->drawpix_cache.entries); i++) {
      const struct drawpix_cache_entry *entry = &st->drawpix_cache.entries[i];
      if (entry->age < oldest_age) {
         oldest_age = entry->age;
         oldest_index = i;
      }
   }

   assert(oldest_index != ~0u);

   return &st->drawpix_cache.entries[oldest_index];
}


/**
 * Try to save the given glDrawPixels image in the cache.
 */
static void
cache_drawpixels_image(struct st_context *st,
                       GLsizei width, GLsizei height,
                       GLenum format, GLenum type,
                       const struct gl_pixelstore_attrib *unpack,
                       const void *pixels,
                       struct pipe_resource *pt)
{
   if ((unpack->RowLength == 0 || unpack->RowLength == width) &&
       unpack->SkipPixels == 0 &&
       unpack->SkipRows == 0) {
      const GLint bpp = _mesa_bytes_per_pixel(format, type);
      struct drawpix_cache_entry *entry =
         find_oldest_drawpixels_cache_entry(st);
      assert(entry);
      entry->width = width;
      entry->height = height;
      entry->format = format;
      entry->type = type;
      entry->user_pointer = pixels;
      free(entry->image);
      entry->image = malloc(width * height * bpp);
      if (entry->image) {
         memcpy(entry->image, pixels, width * height * bpp);
         pipe_resource_reference(&entry->texture, pt);
         entry->age = ++st->drawpix_cache.age;
      }
      else {
         /* out of memory, free/disable cached texture */
         entry->width = 0;
         entry->height = 0;
         pipe_resource_reference(&entry->texture, NULL);
      }
   }
}


/**
 * Make texture containing an image for glDrawPixels image.
 * If 'pixels' is NULL, leave the texture image data undefined.
 */
static struct pipe_resource *
make_texture(struct st_context *st,
	     GLsizei width, GLsizei height, GLenum format, GLenum type,
	     const struct gl_pixelstore_attrib *unpack,
	     const void *pixels)
{
   struct gl_context *ctx = st->ctx;
   struct pipe_context *pipe = st->pipe;
   mesa_format mformat;
   struct pipe_resource *pt = NULL;
   enum pipe_format pipeFormat;
   GLenum baseInternalFormat;

#if USE_DRAWPIXELS_CACHE
   pt = search_drawpixels_cache(st, width, height, format, type,
                                unpack, pixels);
   if (pt) {
      return pt;
   }
#endif

   /* Choose a pixel format for the temp texture which will hold the
    * image to draw.
    */
   pipeFormat = st_choose_matching_format(st, PIPE_BIND_SAMPLER_VIEW,
                                          format, type, unpack->SwapBytes);

   if (pipeFormat == PIPE_FORMAT_NONE) {
      /* Use the generic approach. */
      GLenum intFormat = internal_format(ctx, format, type);

      pipeFormat = st_choose_format(st, intFormat, format, type,
                                    st->internal_target, 0,
                                    PIPE_BIND_SAMPLER_VIEW, FALSE);
      assert(pipeFormat != PIPE_FORMAT_NONE);
   }

   mformat = st_pipe_format_to_mesa_format(pipeFormat);
   baseInternalFormat = _mesa_get_format_base_format(mformat);

   pixels = _mesa_map_pbo_source(ctx, unpack, pixels);
   if (!pixels)
      return NULL;

   /* alloc temporary texture */
   pt = alloc_texture(st, width, height, pipeFormat, PIPE_BIND_SAMPLER_VIEW);
   if (!pt) {
      _mesa_unmap_pbo_source(ctx, unpack);
      return NULL;
   }

   {
      struct pipe_transfer *transfer;
      GLubyte *dest;
      const GLbitfield imageTransferStateSave = ctx->_ImageTransferState;

      /* we'll do pixel transfer in a fragment shader */
      ctx->_ImageTransferState = 0x0;

      /* map texture transfer */
      dest = pipe_transfer_map(pipe, pt, 0, 0,
                               PIPE_TRANSFER_WRITE, 0, 0,
                               width, height, &transfer);


      /* Put image into texture transfer.
       * Note that the image is actually going to be upside down in
       * the texture.  We deal with that with texcoords.
       */
      if ((format == GL_RGBA || format == GL_BGRA)
          && type == GL_UNSIGNED_BYTE) {
         /* Use a memcpy-based texstore to avoid software pixel swizzling.
          * We'll do the necessary swizzling with the pipe_sampler_view to
          * give much better performance.
          * XXX in the future, expand this to accomodate more format and
          * type combinations.
          */
         _mesa_memcpy_texture(ctx, 2,
                              mformat,          /* mesa_format */
                              transfer->stride, /* dstRowStride, bytes */
                              &dest,            /* destSlices */
                              width, height, 1, /* size */
                              format, type,     /* src format/type */
                              pixels,           /* data source */
                              unpack);
      }
      else {
         bool MAYBE_UNUSED success;
         success = _mesa_texstore(ctx, 2,           /* dims */
                                  baseInternalFormat, /* baseInternalFormat */
                                  mformat,          /* mesa_format */
                                  transfer->stride, /* dstRowStride, bytes */
                                  &dest,            /* destSlices */
                                  width, height, 1, /* size */
                                  format, type,     /* src format/type */
                                  pixels,           /* data source */
                                  unpack);

         assert(success);
      }

      /* unmap */
      pipe_transfer_unmap(pipe, transfer);

      /* restore */
      ctx->_ImageTransferState = imageTransferStateSave;
   }

   _mesa_unmap_pbo_source(ctx, unpack);

#if USE_DRAWPIXELS_CACHE
   cache_drawpixels_image(st, width, height, format, type, unpack, pixels, pt);
#endif

   return pt;
}


static void
draw_textured_quad(struct gl_context *ctx, GLint x, GLint y, GLfloat z,
                   GLsizei width, GLsizei height,
                   GLfloat zoomX, GLfloat zoomY,
                   struct pipe_sampler_view **sv,
                   int num_sampler_view,
                   void *driver_vp,
                   void *driver_fp,
                   struct st_fp_variant *fpv,
                   const GLfloat *color,
                   GLboolean invertTex,
                   GLboolean write_depth, GLboolean write_stencil)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct cso_context *cso = st->cso_context;
   const unsigned fb_width = _mesa_geometric_width(ctx->DrawBuffer);
   const unsigned fb_height = _mesa_geometric_height(ctx->DrawBuffer);
   GLfloat x0, y0, x1, y1;
   GLsizei MAYBE_UNUSED maxSize;
   boolean normalized = sv[0]->texture->target == PIPE_TEXTURE_2D;
   unsigned cso_state_mask;

   assert(sv[0]->texture->target == st->internal_target);

   /* limit checks */
   /* XXX if DrawPixels image is larger than max texture size, break
    * it up into chunks.
    */
   maxSize = 1 << (pipe->screen->get_param(pipe->screen,
                                        PIPE_CAP_MAX_TEXTURE_2D_LEVELS) - 1);
   assert(width <= maxSize);
   assert(height <= maxSize);

   cso_state_mask = (CSO_BIT_RASTERIZER |
                     CSO_BIT_VIEWPORT |
                     CSO_BIT_FRAGMENT_SAMPLERS |
                     CSO_BIT_FRAGMENT_SAMPLER_VIEWS |
                     CSO_BIT_STREAM_OUTPUTS |
                     CSO_BIT_VERTEX_ELEMENTS |
                     CSO_BIT_AUX_VERTEX_BUFFER_SLOT |
                     CSO_BITS_ALL_SHADERS);
   if (write_stencil) {
      cso_state_mask |= (CSO_BIT_DEPTH_STENCIL_ALPHA |
                         CSO_BIT_BLEND);
   }
   cso_save_state(cso, cso_state_mask);

   /* rasterizer state: just scissor */
   {
      struct pipe_rasterizer_state rasterizer;
      memset(&rasterizer, 0, sizeof(rasterizer));
      rasterizer.clamp_fragment_color = !st->clamp_frag_color_in_shader &&
                                        ctx->Color._ClampFragmentColor;
      rasterizer.half_pixel_center = 1;
      rasterizer.bottom_edge_rule = 1;
      rasterizer.depth_clip = !ctx->Transform.DepthClamp;
      rasterizer.scissor = ctx->Scissor.EnableFlags;
      cso_set_rasterizer(cso, &rasterizer);
   }

   if (write_stencil) {
      /* Stencil writing bypasses the normal fragment pipeline to
       * disable color writing and set stencil test to always pass.
       */
      struct pipe_depth_stencil_alpha_state dsa;
      struct pipe_blend_state blend;

      /* depth/stencil */
      memset(&dsa, 0, sizeof(dsa));
      dsa.stencil[0].enabled = 1;
      dsa.stencil[0].func = PIPE_FUNC_ALWAYS;
      dsa.stencil[0].writemask = ctx->Stencil.WriteMask[0] & 0xff;
      dsa.stencil[0].zpass_op = PIPE_STENCIL_OP_REPLACE;
      if (write_depth) {
         /* writing depth+stencil: depth test always passes */
         dsa.depth.enabled = 1;
         dsa.depth.writemask = ctx->Depth.Mask;
         dsa.depth.func = PIPE_FUNC_ALWAYS;
      }
      cso_set_depth_stencil_alpha(cso, &dsa);

      /* blend (colormask) */
      memset(&blend, 0, sizeof(blend));
      cso_set_blend(cso, &blend);
   }

   /* fragment shader state: TEX lookup program */
   cso_set_fragment_shader_handle(cso, driver_fp);

   /* vertex shader state: position + texcoord pass-through */
   cso_set_vertex_shader_handle(cso, driver_vp);

   /* disable other shaders */
   cso_set_tessctrl_shader_handle(cso, NULL);
   cso_set_tesseval_shader_handle(cso, NULL);
   cso_set_geometry_shader_handle(cso, NULL);

   /* user samplers, plus the drawpix samplers */
   {
      struct pipe_sampler_state sampler;

      memset(&sampler, 0, sizeof(sampler));
      sampler.wrap_s = PIPE_TEX_WRAP_CLAMP;
      sampler.wrap_t = PIPE_TEX_WRAP_CLAMP;
      sampler.wrap_r = PIPE_TEX_WRAP_CLAMP;
      sampler.min_img_filter = PIPE_TEX_FILTER_NEAREST;
      sampler.min_mip_filter = PIPE_TEX_MIPFILTER_NONE;
      sampler.mag_img_filter = PIPE_TEX_FILTER_NEAREST;
      sampler.normalized_coords = normalized;

      if (fpv) {
         /* drawing a color image */
         const struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
         uint num = MAX3(fpv->drawpix_sampler + 1,
                         fpv->pixelmap_sampler + 1,
                         st->state.num_frag_samplers);
         uint i;

         for (i = 0; i < st->state.num_frag_samplers; i++)
            samplers[i] = &st->state.frag_samplers[i];

         samplers[fpv->drawpix_sampler] = &sampler;
         if (sv[1])
            samplers[fpv->pixelmap_sampler] = &sampler;

         cso_set_samplers(cso, PIPE_SHADER_FRAGMENT, num, samplers);
      } else {
         /* drawing a depth/stencil image */
         const struct pipe_sampler_state *samplers[2] = {&sampler, &sampler};

         cso_set_samplers(cso, PIPE_SHADER_FRAGMENT, num_sampler_view, samplers);
      }
   }

   /* user textures, plus the drawpix textures */
   if (fpv) {
      /* drawing a color image */
      struct pipe_sampler_view *sampler_views[PIPE_MAX_SAMPLERS];
      uint num = MAX3(fpv->drawpix_sampler + 1,
                      fpv->pixelmap_sampler + 1,
                      st->state.num_sampler_views[PIPE_SHADER_FRAGMENT]);

      memcpy(sampler_views, st->state.frag_sampler_views,
             sizeof(sampler_views));

      sampler_views[fpv->drawpix_sampler] = sv[0];
      if (sv[1])
         sampler_views[fpv->pixelmap_sampler] = sv[1];
      cso_set_sampler_views(cso, PIPE_SHADER_FRAGMENT, num, sampler_views);
   } else {
      /* drawing a depth/stencil image */
      cso_set_sampler_views(cso, PIPE_SHADER_FRAGMENT, num_sampler_view, sv);
   }

   /* viewport state: viewport matching window dims */
   cso_set_viewport_dims(cso, fb_width, fb_height, TRUE);

   cso_set_vertex_elements(cso, 3, st->util_velems);
   cso_set_stream_outputs(cso, 0, NULL, NULL);

   /* Compute Gallium window coords (y=0=top) with pixel zoom.
    * Recall that these coords are transformed by the current
    * vertex shader and viewport transformation.
    */
   if (st_fb_orientation(ctx->DrawBuffer) == Y_0_BOTTOM) {
      y = fb_height - (int) (y + height * ctx->Pixel.ZoomY);
      invertTex = !invertTex;
   }

   x0 = (GLfloat) x;
   x1 = x + width * ctx->Pixel.ZoomX;
   y0 = (GLfloat) y;
   y1 = y + height * ctx->Pixel.ZoomY;

   /* convert Z from [0,1] to [-1,-1] to match viewport Z scale/bias */
   z = z * 2.0f - 1.0f;

   {
      const float clip_x0 = x0 / (float) fb_width * 2.0f - 1.0f;
      const float clip_y0 = y0 / (float) fb_height * 2.0f - 1.0f;
      const float clip_x1 = x1 / (float) fb_width * 2.0f - 1.0f;
      const float clip_y1 = y1 / (float) fb_height * 2.0f - 1.0f;
      const float maxXcoord = normalized ?
         ((float) width / sv[0]->texture->width0) : (float) width;
      const float maxYcoord = normalized
         ? ((float) height / sv[0]->texture->height0) : (float) height;
      const float sLeft = 0.0f, sRight = maxXcoord;
      const float tTop = invertTex ? maxYcoord : 0.0f;
      const float tBot = invertTex ? 0.0f : maxYcoord;

      if (!st_draw_quad(st, clip_x0, clip_y0, clip_x1, clip_y1, z,
                        sLeft, tBot, sRight, tTop, color, 0)) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glDrawPixels");
      }
   }

   /* restore state */
   cso_restore_state(cso);
}


/**
 * Software fallback to do glDrawPixels(GL_STENCIL_INDEX) when we
 * can't use a fragment shader to write stencil values.
 */
static void
draw_stencil_pixels(struct gl_context *ctx, GLint x, GLint y,
                    GLsizei width, GLsizei height, GLenum format, GLenum type,
                    const struct gl_pixelstore_attrib *unpack,
                    const void *pixels)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct st_renderbuffer *strb;
   enum pipe_transfer_usage usage;
   struct pipe_transfer *pt;
   const GLboolean zoom = ctx->Pixel.ZoomX != 1.0 || ctx->Pixel.ZoomY != 1.0;
   ubyte *stmap;
   struct gl_pixelstore_attrib clippedUnpack = *unpack;
   GLubyte *sValues;
   GLuint *zValues;

   if (!zoom) {
      if (!_mesa_clip_drawpixels(ctx, &x, &y, &width, &height,
                                 &clippedUnpack)) {
         /* totally clipped */
         return;
      }
   }

   strb = st_renderbuffer(ctx->DrawBuffer->
                          Attachment[BUFFER_STENCIL].Renderbuffer);

   if (st_fb_orientation(ctx->DrawBuffer) == Y_0_TOP) {
      y = ctx->DrawBuffer->Height - y - height;
   }

   if (format == GL_STENCIL_INDEX && 
       _mesa_is_format_packed_depth_stencil(strb->Base.Format)) {
      /* writing stencil to a combined depth+stencil buffer */
      usage = PIPE_TRANSFER_READ_WRITE;
   }
   else {
      usage = PIPE_TRANSFER_WRITE;
   }

   stmap = pipe_transfer_map(pipe, strb->texture,
                             strb->surface->u.tex.level,
                             strb->surface->u.tex.first_layer,
                             usage, x, y,
                             width, height, &pt);

   pixels = _mesa_map_pbo_source(ctx, &clippedUnpack, pixels);
   assert(pixels);

   sValues = malloc(width * sizeof(GLubyte));
   zValues = malloc(width * sizeof(GLuint));

   if (sValues && zValues) {
      GLint row;
      for (row = 0; row < height; row++) {
         GLfloat *zValuesFloat = (GLfloat*)zValues;
         GLenum destType = GL_UNSIGNED_BYTE;
         const void *source = _mesa_image_address2d(&clippedUnpack, pixels,
                                                      width, height,
                                                      format, type,
                                                      row, 0);
         _mesa_unpack_stencil_span(ctx, width, destType, sValues,
                                   type, source, &clippedUnpack,
                                   ctx->_ImageTransferState);

         if (format == GL_DEPTH_STENCIL) {
            GLenum ztype =
               pt->resource->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ?
               GL_FLOAT : GL_UNSIGNED_INT;

            _mesa_unpack_depth_span(ctx, width, ztype, zValues,
                                    (1 << 24) - 1, type, source,
                                    &clippedUnpack);
         }

         if (zoom) {
            _mesa_problem(ctx, "Gallium glDrawPixels(GL_STENCIL) with "
                          "zoom not complete");
         }

         {
            GLint spanY;

            if (st_fb_orientation(ctx->DrawBuffer) == Y_0_TOP) {
               spanY = height - row - 1;
            }
            else {
               spanY = row;
            }

            /* now pack the stencil (and Z) values in the dest format */
            switch (pt->resource->format) {
            case PIPE_FORMAT_S8_UINT:
               {
                  ubyte *dest = stmap + spanY * pt->stride;
                  assert(usage == PIPE_TRANSFER_WRITE);
                  memcpy(dest, sValues, width);
               }
               break;
            case PIPE_FORMAT_Z24_UNORM_S8_UINT:
               if (format == GL_DEPTH_STENCIL) {
                  uint *dest = (uint *) (stmap + spanY * pt->stride);
                  GLint k;
                  assert(usage == PIPE_TRANSFER_WRITE);
                  for (k = 0; k < width; k++) {
                     dest[k] = zValues[k] | (sValues[k] << 24);
                  }
               }
               else {
                  uint *dest = (uint *) (stmap + spanY * pt->stride);
                  GLint k;
                  assert(usage == PIPE_TRANSFER_READ_WRITE);
                  for (k = 0; k < width; k++) {
                     dest[k] = (dest[k] & 0xffffff) | (sValues[k] << 24);
                  }
               }
               break;
            case PIPE_FORMAT_S8_UINT_Z24_UNORM:
               if (format == GL_DEPTH_STENCIL) {
                  uint *dest = (uint *) (stmap + spanY * pt->stride);
                  GLint k;
                  assert(usage == PIPE_TRANSFER_WRITE);
                  for (k = 0; k < width; k++) {
                     dest[k] = (zValues[k] << 8) | (sValues[k] & 0xff);
                  }
               }
               else {
                  uint *dest = (uint *) (stmap + spanY * pt->stride);
                  GLint k;
                  assert(usage == PIPE_TRANSFER_READ_WRITE);
                  for (k = 0; k < width; k++) {
                     dest[k] = (dest[k] & 0xffffff00) | (sValues[k] & 0xff);
                  }
               }
               break;
            case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
               if (format == GL_DEPTH_STENCIL) {
                  uint *dest = (uint *) (stmap + spanY * pt->stride);
                  GLfloat *destf = (GLfloat*)dest;
                  GLint k;
                  assert(usage == PIPE_TRANSFER_WRITE);
                  for (k = 0; k < width; k++) {
                     destf[k*2] = zValuesFloat[k];
                     dest[k*2+1] = sValues[k] & 0xff;
                  }
               }
               else {
                  uint *dest = (uint *) (stmap + spanY * pt->stride);
                  GLint k;
                  assert(usage == PIPE_TRANSFER_READ_WRITE);
                  for (k = 0; k < width; k++) {
                     dest[k*2+1] = sValues[k] & 0xff;
                  }
               }
               break;
            default:
               assert(0);
            }
         }
      }
   }
   else {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glDrawPixels()");
   }

   free(sValues);
   free(zValues);

   _mesa_unmap_pbo_source(ctx, &clippedUnpack);

   /* unmap the stencil buffer */
   pipe_transfer_unmap(pipe, pt);
}


/**
 * Get fragment program variant for a glDrawPixels or glCopyPixels
 * command for RGBA data.
 */
static struct st_fp_variant *
get_color_fp_variant(struct st_context *st)
{
   struct gl_context *ctx = st->ctx;
   struct st_fp_variant_key key;
   struct st_fp_variant *fpv;

   memset(&key, 0, sizeof(key));

   key.st = st->has_shareable_shaders ? NULL : st;
   key.drawpixels = 1;
   key.scaleAndBias = (ctx->Pixel.RedBias != 0.0 ||
                       ctx->Pixel.RedScale != 1.0 ||
                       ctx->Pixel.GreenBias != 0.0 ||
                       ctx->Pixel.GreenScale != 1.0 ||
                       ctx->Pixel.BlueBias != 0.0 ||
                       ctx->Pixel.BlueScale != 1.0 ||
                       ctx->Pixel.AlphaBias != 0.0 ||
                       ctx->Pixel.AlphaScale != 1.0);
   key.pixelMaps = ctx->Pixel.MapColorFlag;
   key.clamp_color = st->clamp_frag_color_in_shader &&
                     ctx->Color._ClampFragmentColor;

   fpv = st_get_fp_variant(st, st->fp, &key);

   return fpv;
}


/**
 * Clamp glDrawPixels width and height to the maximum texture size.
 */
static void
clamp_size(struct pipe_context *pipe, GLsizei *width, GLsizei *height,
           struct gl_pixelstore_attrib *unpack)
{
   const int maxSize =
      1 << (pipe->screen->get_param(pipe->screen,
                                    PIPE_CAP_MAX_TEXTURE_2D_LEVELS) - 1);

   if (*width > maxSize) {
      if (unpack->RowLength == 0)
         unpack->RowLength = *width;
      *width = maxSize;
   }
   if (*height > maxSize) {
      *height = maxSize;
   }
}


/**
 * Search the array of 4 swizzle components for the named component and return
 * its position.
 */
static unsigned
search_swizzle(const unsigned char swizzle[4], unsigned component)
{
   unsigned i;
   for (i = 0; i < 4; i++) {
      if (swizzle[i] == component)
         return i;
   }
   assert(!"search_swizzle() failed");
   return 0;
}


/**
 * Set the sampler view's swizzle terms.  This is used to handle RGBA
 * swizzling when the incoming image format isn't an exact match for
 * the actual texture format.  For example, if we have glDrawPixels(
 * GL_RGBA, GL_UNSIGNED_BYTE) and we chose the texture format
 * PIPE_FORMAT_B8G8R8A8 then we can do use the sampler view swizzle to
 * avoid swizzling all the pixels in software in the texstore code.
 */
static void
setup_sampler_swizzle(struct pipe_sampler_view *sv, GLenum format, GLenum type)
{
   if ((format == GL_RGBA || format == GL_BGRA) && type == GL_UNSIGNED_BYTE) {
      const struct util_format_description *desc =
         util_format_description(sv->texture->format);
      unsigned c0, c1, c2, c3;

      /* Every gallium driver supports at least one 32-bit packed RGBA format.
       * We must have chosen one for (GL_RGBA, GL_UNSIGNED_BYTE).
       */
      assert(desc->block.bits == 32);

      /* invert the format's swizzle to setup the sampler's swizzle */
      if (format == GL_RGBA) {
         c0 = PIPE_SWIZZLE_X;
         c1 = PIPE_SWIZZLE_Y;
         c2 = PIPE_SWIZZLE_Z;
         c3 = PIPE_SWIZZLE_W;
      }
      else {
         assert(format == GL_BGRA);
         c0 = PIPE_SWIZZLE_Z;
         c1 = PIPE_SWIZZLE_Y;
         c2 = PIPE_SWIZZLE_X;
         c3 = PIPE_SWIZZLE_W;
      }
      sv->swizzle_r = search_swizzle(desc->swizzle, c0);
      sv->swizzle_g = search_swizzle(desc->swizzle, c1);
      sv->swizzle_b = search_swizzle(desc->swizzle, c2);
      sv->swizzle_a = search_swizzle(desc->swizzle, c3);
   }
   else {
      /* use the default sampler swizzle */
   }
}


/**
 * Called via ctx->Driver.DrawPixels()
 */
static void
st_DrawPixels(struct gl_context *ctx, GLint x, GLint y,
              GLsizei width, GLsizei height,
              GLenum format, GLenum type,
              const struct gl_pixelstore_attrib *unpack, const void *pixels)
{
   void *driver_vp, *driver_fp;
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   GLboolean write_stencil = GL_FALSE, write_depth = GL_FALSE;
   struct pipe_sampler_view *sv[2] = { NULL };
   int num_sampler_view = 1;
   struct gl_pixelstore_attrib clippedUnpack;
   struct st_fp_variant *fpv = NULL;
   struct pipe_resource *pt;

   /* Mesa state should be up to date by now */
   assert(ctx->NewState == 0x0);

   _mesa_update_draw_buffer_bounds(ctx, ctx->DrawBuffer);

   st_flush_bitmap_cache(st);
   st_invalidate_readpix_cache(st);

   st_validate_state(st, ST_PIPELINE_META);

   /* Limit the size of the glDrawPixels to the max texture size.
    * Strictly speaking, that's not correct but since we don't handle
    * larger images yet, this is better than crashing.
    */
   clippedUnpack = *unpack;
   unpack = &clippedUnpack;
   clamp_size(st->pipe, &width, &height, &clippedUnpack);

   if (format == GL_DEPTH_STENCIL)
      write_stencil = write_depth = GL_TRUE;
   else if (format == GL_STENCIL_INDEX)
      write_stencil = GL_TRUE;
   else if (format == GL_DEPTH_COMPONENT)
      write_depth = GL_TRUE;

   if (write_stencil &&
       !pipe->screen->get_param(pipe->screen, PIPE_CAP_SHADER_STENCIL_EXPORT)) {
      /* software fallback */
      draw_stencil_pixels(ctx, x, y, width, height, format, type,
                          unpack, pixels);
      return;
   }

   /*
    * Get vertex/fragment shaders
    */
   if (write_depth || write_stencil) {
      driver_fp = get_drawpix_z_stencil_program(st, write_depth,
                                                write_stencil);
      driver_vp = make_passthrough_vertex_shader(st, GL_TRUE);
   }
   else {
      fpv = get_color_fp_variant(st);

      driver_fp = fpv->driver_shader;
      driver_vp = make_passthrough_vertex_shader(st, GL_FALSE);

      if (ctx->Pixel.MapColorFlag) {
         pipe_sampler_view_reference(&sv[1],
                                     st->pixel_xfer.pixelmap_sampler_view);
         num_sampler_view++;
      }

      /* compiling a new fragment shader variant added new state constants
       * into the constant buffer, we need to update them
       */
      st_upload_constants(st, &st->fp->Base);
   }

   /* Put glDrawPixels image into a texture */
   pt = make_texture(st, width, height, format, type, unpack, pixels);
   if (!pt) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glDrawPixels");
      return;
   }

   /* create sampler view for the image */
   sv[0] = st_create_texture_sampler_view(st->pipe, pt);
   if (!sv[0]) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glDrawPixels");
      pipe_resource_reference(&pt, NULL);
      return;
   }

   /* Set up the sampler view's swizzle */
   setup_sampler_swizzle(sv[0], format, type);

   /* Create a second sampler view to read stencil.  The stencil is
    * written using the shader stencil export functionality.
    */
   if (write_stencil) {
      enum pipe_format stencil_format =
         util_format_stencil_only(pt->format);
      /* we should not be doing pixel map/transfer (see above) */
      assert(num_sampler_view == 1);
      sv[1] = st_create_texture_sampler_view_format(st->pipe, pt,
                                                    stencil_format);
      if (!sv[1]) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glDrawPixels");
         pipe_resource_reference(&pt, NULL);
         pipe_sampler_view_reference(&sv[0], NULL);
         return;
      }
      num_sampler_view++;
   }

   draw_textured_quad(ctx, x, y, ctx->Current.RasterPos[2],
                      width, height,
                      ctx->Pixel.ZoomX, ctx->Pixel.ZoomY,
                      sv,
                      num_sampler_view,
                      driver_vp,
                      driver_fp, fpv,
                      ctx->Current.RasterColor,
                      GL_FALSE, write_depth, write_stencil);
   pipe_sampler_view_reference(&sv[0], NULL);
   if (num_sampler_view > 1)
      pipe_sampler_view_reference(&sv[1], NULL);

   /* free the texture (but may persist in the cache) */
   pipe_resource_reference(&pt, NULL);
}



/**
 * Software fallback for glCopyPixels(GL_STENCIL).
 */
static void
copy_stencil_pixels(struct gl_context *ctx, GLint srcx, GLint srcy,
                    GLsizei width, GLsizei height,
                    GLint dstx, GLint dsty)
{
   struct st_renderbuffer *rbDraw;
   struct pipe_context *pipe = st_context(ctx)->pipe;
   enum pipe_transfer_usage usage;
   struct pipe_transfer *ptDraw;
   ubyte *drawMap;
   ubyte *buffer;
   int i;

   buffer = malloc(width * height * sizeof(ubyte));
   if (!buffer) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glCopyPixels(stencil)");
      return;
   }

   /* Get the dest renderbuffer */
   rbDraw = st_renderbuffer(ctx->DrawBuffer->
                            Attachment[BUFFER_STENCIL].Renderbuffer);

   /* this will do stencil pixel transfer ops */
   _mesa_readpixels(ctx, srcx, srcy, width, height,
                    GL_STENCIL_INDEX, GL_UNSIGNED_BYTE,
                    &ctx->DefaultPacking, buffer);

   if (0) {
      /* debug code: dump stencil values */
      GLint row, col;
      for (row = 0; row < height; row++) {
         printf("%3d: ", row);
         for (col = 0; col < width; col++) {
            printf("%02x ", buffer[col + row * width]);
         }
         printf("\n");
      }
   }

   if (_mesa_is_format_packed_depth_stencil(rbDraw->Base.Format))
      usage = PIPE_TRANSFER_READ_WRITE;
   else
      usage = PIPE_TRANSFER_WRITE;

   if (st_fb_orientation(ctx->DrawBuffer) == Y_0_TOP) {
      dsty = rbDraw->Base.Height - dsty - height;
   }

   assert(util_format_get_blockwidth(rbDraw->texture->format) == 1);
   assert(util_format_get_blockheight(rbDraw->texture->format) == 1);

   /* map the stencil buffer */
   drawMap = pipe_transfer_map(pipe,
                               rbDraw->texture,
                               rbDraw->surface->u.tex.level,
                               rbDraw->surface->u.tex.first_layer,
                               usage, dstx, dsty,
                               width, height, &ptDraw);

   /* draw */
   /* XXX PixelZoom not handled yet */
   for (i = 0; i < height; i++) {
      ubyte *dst;
      const ubyte *src;
      int y;

      y = i;

      if (st_fb_orientation(ctx->DrawBuffer) == Y_0_TOP) {
         y = height - y - 1;
      }

      dst = drawMap + y * ptDraw->stride;
      src = buffer + i * width;

      _mesa_pack_ubyte_stencil_row(rbDraw->Base.Format, width, src, dst);
   }

   free(buffer);

   /* unmap the stencil buffer */
   pipe_transfer_unmap(pipe, ptDraw);
}


/**
 * Return renderbuffer to use for reading color pixels for glCopyPixels
 */
static struct st_renderbuffer *
st_get_color_read_renderbuffer(struct gl_context *ctx)
{
   struct gl_framebuffer *fb = ctx->ReadBuffer;
   struct st_renderbuffer *strb =
      st_renderbuffer(fb->_ColorReadBuffer);

   return strb;
}


/**
 * Try to do a glCopyPixels for simple cases with a blit by calling
 * pipe->blit().
 *
 * We can do this when we're copying color pixels (depth/stencil
 * eventually) with no pixel zoom, no pixel transfer ops, no
 * per-fragment ops, and the src/dest regions don't overlap.
 */
static GLboolean
blit_copy_pixels(struct gl_context *ctx, GLint srcx, GLint srcy,
                 GLsizei width, GLsizei height,
                 GLint dstx, GLint dsty, GLenum type)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct gl_pixelstore_attrib pack, unpack;
   GLint readX, readY, readW, readH, drawX, drawY, drawW, drawH;

   if (type == GL_COLOR &&
       ctx->Pixel.ZoomX == 1.0 &&
       ctx->Pixel.ZoomY == 1.0 &&
       ctx->_ImageTransferState == 0x0 &&
       !ctx->Color.BlendEnabled &&
       !ctx->Color.AlphaEnabled &&
       (!ctx->Color.ColorLogicOpEnabled || ctx->Color.LogicOp == GL_COPY) &&
       !ctx->Depth.Test &&
       !ctx->Fog.Enabled &&
       !ctx->Stencil.Enabled &&
       !ctx->FragmentProgram.Enabled &&
       !ctx->VertexProgram.Enabled &&
       !ctx->_Shader->CurrentProgram[MESA_SHADER_FRAGMENT] &&
       !_mesa_ati_fragment_shader_enabled(ctx) &&
       ctx->DrawBuffer->_NumColorDrawBuffers == 1 &&
       !ctx->Query.CondRenderQuery &&
       !ctx->Query.CurrentOcclusionObject) {
      struct st_renderbuffer *rbRead, *rbDraw;

      /*
       * Clip the read region against the src buffer bounds.
       * We'll still allocate a temporary buffer/texture for the original
       * src region size but we'll only read the region which is on-screen.
       * This may mean that we draw garbage pixels into the dest region, but
       * that's expected.
       */
      readX = srcx;
      readY = srcy;
      readW = width;
      readH = height;
      pack = ctx->DefaultPacking;
      if (!_mesa_clip_readpixels(ctx, &readX, &readY, &readW, &readH, &pack))
         return GL_TRUE; /* all done */

      /* clip against dest buffer bounds and scissor box */
      drawX = dstx + pack.SkipPixels;
      drawY = dsty + pack.SkipRows;
      unpack = pack;
      if (!_mesa_clip_drawpixels(ctx, &drawX, &drawY, &readW, &readH, &unpack))
         return GL_TRUE; /* all done */

      readX = readX - pack.SkipPixels + unpack.SkipPixels;
      readY = readY - pack.SkipRows + unpack.SkipRows;

      drawW = readW;
      drawH = readH;

      rbRead = st_get_color_read_renderbuffer(ctx);
      rbDraw = st_renderbuffer(ctx->DrawBuffer->_ColorDrawBuffers[0]);

      /* Flip src/dst position depending on the orientation of buffers. */
      if (st_fb_orientation(ctx->ReadBuffer) == Y_0_TOP) {
         readY = rbRead->Base.Height - readY;
         readH = -readH;
      }

      if (st_fb_orientation(ctx->DrawBuffer) == Y_0_TOP) {
         /* We can't flip the destination for pipe->blit, so we only adjust
          * its position and flip the source.
          */
         drawY = rbDraw->Base.Height - drawY - drawH;
         readY += readH;
         readH = -readH;
      }

      if (rbRead != rbDraw ||
          !_mesa_regions_overlap(readX, readY, readX + readW, readY + readH,
                                 drawX, drawY, drawX + drawW, drawY + drawH)) {
         struct pipe_blit_info blit;

         memset(&blit, 0, sizeof(blit));
         blit.src.resource = rbRead->texture;
         blit.src.level = rbRead->surface->u.tex.level;
         blit.src.format = rbRead->texture->format;
         blit.src.box.x = readX;
         blit.src.box.y = readY;
         blit.src.box.z = rbRead->surface->u.tex.first_layer;
         blit.src.box.width = readW;
         blit.src.box.height = readH;
         blit.src.box.depth = 1;
         blit.dst.resource = rbDraw->texture;
         blit.dst.level = rbDraw->surface->u.tex.level;
         blit.dst.format = rbDraw->texture->format;
         blit.dst.box.x = drawX;
         blit.dst.box.y = drawY;
         blit.dst.box.z = rbDraw->surface->u.tex.first_layer;
         blit.dst.box.width = drawW;
         blit.dst.box.height = drawH;
         blit.dst.box.depth = 1;
         blit.mask = PIPE_MASK_RGBA;
         blit.filter = PIPE_TEX_FILTER_NEAREST;

         if (ctx->DrawBuffer != ctx->WinSysDrawBuffer)
            st_window_rectangles_to_blit(ctx, &blit);

         if (screen->is_format_supported(screen, blit.src.format,
                                         blit.src.resource->target,
                                         blit.src.resource->nr_samples,
                                         PIPE_BIND_SAMPLER_VIEW) &&
             screen->is_format_supported(screen, blit.dst.format,
                                         blit.dst.resource->target,
                                         blit.dst.resource->nr_samples,
                                         PIPE_BIND_RENDER_TARGET)) {
            pipe->blit(pipe, &blit);
            return GL_TRUE;
         }
      }
   }

   return GL_FALSE;
}


static void
st_CopyPixels(struct gl_context *ctx, GLint srcx, GLint srcy,
              GLsizei width, GLsizei height,
              GLint dstx, GLint dsty, GLenum type)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;
   struct st_renderbuffer *rbRead;
   void *driver_vp, *driver_fp;
   struct pipe_resource *pt;
   struct pipe_sampler_view *sv[2] = { NULL };
   struct st_fp_variant *fpv = NULL;
   int num_sampler_view = 1;
   enum pipe_format srcFormat;
   unsigned srcBind;
   GLboolean invertTex = GL_FALSE;
   GLint readX, readY, readW, readH;
   struct gl_pixelstore_attrib pack = ctx->DefaultPacking;

   _mesa_update_draw_buffer_bounds(ctx, ctx->DrawBuffer);

   st_flush_bitmap_cache(st);
   st_invalidate_readpix_cache(st);

   st_validate_state(st, ST_PIPELINE_META);

   if (type == GL_DEPTH_STENCIL) {
      /* XXX make this more efficient */
      st_CopyPixels(ctx, srcx, srcy, width, height, dstx, dsty, GL_STENCIL);
      st_CopyPixels(ctx, srcx, srcy, width, height, dstx, dsty, GL_DEPTH);
      return;
   }

   if (type == GL_STENCIL) {
      /* can't use texturing to do stencil */
      copy_stencil_pixels(ctx, srcx, srcy, width, height, dstx, dsty);
      return;
   }

   if (blit_copy_pixels(ctx, srcx, srcy, width, height, dstx, dsty, type))
      return;

   /*
    * The subsequent code implements glCopyPixels by copying the source
    * pixels into a temporary texture that's then applied to a textured quad.
    * When we draw the textured quad, all the usual per-fragment operations
    * are handled.
    */


   /*
    * Get vertex/fragment shaders
    */
   if (type == GL_COLOR) {
      fpv = get_color_fp_variant(st);

      rbRead = st_get_color_read_renderbuffer(ctx);

      driver_fp = fpv->driver_shader;
      driver_vp = make_passthrough_vertex_shader(st, GL_FALSE);

      if (ctx->Pixel.MapColorFlag) {
         pipe_sampler_view_reference(&sv[1],
                                     st->pixel_xfer.pixelmap_sampler_view);
         num_sampler_view++;
      }

      /* compiling a new fragment shader variant added new state constants
       * into the constant buffer, we need to update them
       */
      st_upload_constants(st, &st->fp->Base);
   }
   else {
      assert(type == GL_DEPTH);
      rbRead = st_renderbuffer(ctx->ReadBuffer->
                               Attachment[BUFFER_DEPTH].Renderbuffer);

      driver_fp = get_drawpix_z_stencil_program(st, GL_TRUE, GL_FALSE);
      driver_vp = make_passthrough_vertex_shader(st, GL_TRUE);
   }

   /* Choose the format for the temporary texture. */
   srcFormat = rbRead->texture->format;
   srcBind = PIPE_BIND_SAMPLER_VIEW |
      (type == GL_COLOR ? PIPE_BIND_RENDER_TARGET : PIPE_BIND_DEPTH_STENCIL);

   if (!screen->is_format_supported(screen, srcFormat, st->internal_target, 0,
                                    srcBind)) {
      /* srcFormat is non-renderable. Find a compatible renderable format. */
      if (type == GL_DEPTH) {
         srcFormat = st_choose_format(st, GL_DEPTH_COMPONENT, GL_NONE,
                                      GL_NONE, st->internal_target, 0,
                                      srcBind, FALSE);
      }
      else {
         assert(type == GL_COLOR);

         if (util_format_is_float(srcFormat)) {
            srcFormat = st_choose_format(st, GL_RGBA32F, GL_NONE,
                                         GL_NONE, st->internal_target, 0,
                                         srcBind, FALSE);
         }
         else if (util_format_is_pure_sint(srcFormat)) {
            srcFormat = st_choose_format(st, GL_RGBA32I, GL_NONE,
                                         GL_NONE, st->internal_target, 0,
                                         srcBind, FALSE);
         }
         else if (util_format_is_pure_uint(srcFormat)) {
            srcFormat = st_choose_format(st, GL_RGBA32UI, GL_NONE,
                                         GL_NONE, st->internal_target, 0,
                                         srcBind, FALSE);
         }
         else if (util_format_is_snorm(srcFormat)) {
            srcFormat = st_choose_format(st, GL_RGBA16_SNORM, GL_NONE,
                                         GL_NONE, st->internal_target, 0,
                                         srcBind, FALSE);
         }
         else {
            srcFormat = st_choose_format(st, GL_RGBA, GL_NONE,
                                         GL_NONE, st->internal_target, 0,
                                         srcBind, FALSE);
         }
      }

      if (srcFormat == PIPE_FORMAT_NONE) {
         assert(0 && "cannot choose a format for src of CopyPixels");
         return;
      }
   }

   /* Invert src region if needed */
   if (st_fb_orientation(ctx->ReadBuffer) == Y_0_TOP) {
      srcy = ctx->ReadBuffer->Height - srcy - height;
      invertTex = !invertTex;
   }

   /* Clip the read region against the src buffer bounds.
    * We'll still allocate a temporary buffer/texture for the original
    * src region size but we'll only read the region which is on-screen.
    * This may mean that we draw garbage pixels into the dest region, but
    * that's expected.
    */
   readX = srcx;
   readY = srcy;
   readW = width;
   readH = height;
   if (!_mesa_clip_readpixels(ctx, &readX, &readY, &readW, &readH, &pack)) {
      /* The source region is completely out of bounds.  Do nothing.
       * The GL spec says "Results of copies from outside the window,
       * or from regions of the window that are not exposed, are
       * hardware dependent and undefined."
       */
      return;
   }

   readW = MAX2(0, readW);
   readH = MAX2(0, readH);

   /* Allocate the temporary texture. */
   pt = alloc_texture(st, width, height, srcFormat, srcBind);
   if (!pt)
      return;

   sv[0] = st_create_texture_sampler_view(st->pipe, pt);
   if (!sv[0]) {
      pipe_resource_reference(&pt, NULL);
      return;
   }

   /* Copy the src region to the temporary texture. */
   {
      struct pipe_blit_info blit;

      memset(&blit, 0, sizeof(blit));
      blit.src.resource = rbRead->texture;
      blit.src.level = rbRead->surface->u.tex.level;
      blit.src.format = rbRead->texture->format;
      blit.src.box.x = readX;
      blit.src.box.y = readY;
      blit.src.box.z = rbRead->surface->u.tex.first_layer;
      blit.src.box.width = readW;
      blit.src.box.height = readH;
      blit.src.box.depth = 1;
      blit.dst.resource = pt;
      blit.dst.level = 0;
      blit.dst.format = pt->format;
      blit.dst.box.x = pack.SkipPixels;
      blit.dst.box.y = pack.SkipRows;
      blit.dst.box.z = 0;
      blit.dst.box.width = readW;
      blit.dst.box.height = readH;
      blit.dst.box.depth = 1;
      blit.mask = util_format_get_mask(pt->format) & ~PIPE_MASK_S;
      blit.filter = PIPE_TEX_FILTER_NEAREST;

      pipe->blit(pipe, &blit);
   }

   /* OK, the texture 'pt' contains the src image/pixels.  Now draw a
    * textured quad with that texture.
    */
   draw_textured_quad(ctx, dstx, dsty, ctx->Current.RasterPos[2],
                      width, height, ctx->Pixel.ZoomX, ctx->Pixel.ZoomY,
                      sv,
                      num_sampler_view,
                      driver_vp, 
                      driver_fp, fpv,
                      ctx->Current.Attrib[VERT_ATTRIB_COLOR0],
                      invertTex, GL_FALSE, GL_FALSE);

   pipe_resource_reference(&pt, NULL);
   pipe_sampler_view_reference(&sv[0], NULL);
}



void st_init_drawpixels_functions(struct dd_function_table *functions)
{
   functions->DrawPixels = st_DrawPixels;
   functions->CopyPixels = st_CopyPixels;
}


void
st_destroy_drawpix(struct st_context *st)
{
   GLuint i;

   for (i = 0; i < ARRAY_SIZE(st->drawpix.zs_shaders); i++) {
      if (st->drawpix.zs_shaders[i])
         cso_delete_fragment_shader(st->cso_context,
                                    st->drawpix.zs_shaders[i]);
   }

   if (st->drawpix.vert_shaders[0])
      cso_delete_vertex_shader(st->cso_context, st->drawpix.vert_shaders[0]);
   if (st->drawpix.vert_shaders[1])
      cso_delete_vertex_shader(st->cso_context, st->drawpix.vert_shaders[1]);

   /* Free cache data */
   for (i = 0; i < ARRAY_SIZE(st->drawpix_cache.entries); i++) {
      struct drawpix_cache_entry *entry = &st->drawpix_cache.entries[i];
      free(entry->image);
      pipe_resource_reference(&entry->texture, NULL);
   }
}
