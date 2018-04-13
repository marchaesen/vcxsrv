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
#include "main/dlist.h"
#include "main/macros.h"
#include "main/pbo.h"
#include "program/program.h"
#include "program/prog_print.h"

#include "st_context.h"
#include "st_atom.h"
#include "st_atom_constbuf.h"
#include "st_draw.h"
#include "st_program.h"
#include "st_cb_bitmap.h"
#include "st_sampler_view.h"
#include "st_texture.h"

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_shader_tokens.h"
#include "util/u_inlines.h"
#include "util/u_simple_shaders.h"
#include "util/u_upload_mgr.h"
#include "program/prog_instruction.h"
#include "cso_cache/cso_context.h"


/**
 * glBitmaps are drawn as textured quads.  The user's bitmap pattern
 * is stored in a texture image.  An alpha8 texture format is used.
 * The fragment shader samples a bit (texel) from the texture, then
 * discards the fragment if the bit is off.
 *
 * Note that we actually store the inverse image of the bitmap to
 * simplify the fragment program.  An "on" bit gets stored as texel=0x0
 * and an "off" bit is stored as texel=0xff.  Then we kill the
 * fragment if the negated texel value is less than zero.
 */


/**
 * The bitmap cache attempts to accumulate multiple glBitmap calls in a
 * buffer which is then rendered en mass upon a flush, state change, etc.
 * A wide, short buffer is used to target the common case of a series
 * of glBitmap calls being used to draw text.
 */
static GLboolean UseBitmapCache = GL_TRUE;


#define BITMAP_CACHE_WIDTH  512
#define BITMAP_CACHE_HEIGHT 32


/** Epsilon for Z comparisons */
#define Z_EPSILON 1e-06


/**
 * Copy user-provide bitmap bits into texture buffer, expanding
 * bits into texels.
 * "On" bits will set texels to 0x0.
 * "Off" bits will not modify texels.
 * Note that the image is actually going to be upside down in
 * the texture.  We deal with that with texcoords.
 */
static void
unpack_bitmap(struct st_context *st,
              GLint px, GLint py, GLsizei width, GLsizei height,
              const struct gl_pixelstore_attrib *unpack,
              const GLubyte *bitmap,
              ubyte *destBuffer, uint destStride)
{
   destBuffer += py * destStride + px;

   _mesa_expand_bitmap(width, height, unpack, bitmap,
                       destBuffer, destStride, 0x0);
}


/**
 * Create a texture which represents a bitmap image.
 */
static struct pipe_resource *
make_bitmap_texture(struct gl_context *ctx, GLsizei width, GLsizei height,
                    const struct gl_pixelstore_attrib *unpack,
                    const GLubyte *bitmap)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_transfer *transfer;
   ubyte *dest;
   struct pipe_resource *pt;

   /* PBO source... */
   bitmap = _mesa_map_pbo_source(ctx, unpack, bitmap);
   if (!bitmap) {
      return NULL;
   }

   /**
    * Create texture to hold bitmap pattern.
    */
   pt = st_texture_create(st, st->internal_target, st->bitmap.tex_format,
                          0, width, height, 1, 1, 0,
                          PIPE_BIND_SAMPLER_VIEW);
   if (!pt) {
      _mesa_unmap_pbo_source(ctx, unpack);
      return NULL;
   }

   dest = pipe_transfer_map(st->pipe, pt, 0, 0,
                            PIPE_TRANSFER_WRITE,
                            0, 0, width, height, &transfer);

   /* Put image into texture transfer */
   memset(dest, 0xff, height * transfer->stride);
   unpack_bitmap(st, 0, 0, width, height, unpack, bitmap,
                 dest, transfer->stride);

   _mesa_unmap_pbo_source(ctx, unpack);

   /* Release transfer */
   pipe_transfer_unmap(pipe, transfer);
   return pt;
}


/**
 * Setup pipeline state prior to rendering the bitmap textured quad.
 */
static void
setup_render_state(struct gl_context *ctx,
                   struct pipe_sampler_view *sv,
                   const GLfloat *color,
                   bool atlas)
{
   struct st_context *st = st_context(ctx);
   struct cso_context *cso = st->cso_context;
   struct st_fp_variant *fpv;
   struct st_fp_variant_key key;

   memset(&key, 0, sizeof(key));
   key.st = st->has_shareable_shaders ? NULL : st;
   key.bitmap = GL_TRUE;
   key.clamp_color = st->clamp_frag_color_in_shader &&
                     ctx->Color._ClampFragmentColor;

   fpv = st_get_fp_variant(st, st->fp, &key);

   /* As an optimization, Mesa's fragment programs will sometimes get the
    * primary color from a statevar/constant rather than a varying variable.
    * when that's the case, we need to ensure that we use the 'color'
    * parameter and not the current attribute color (which may have changed
    * through glRasterPos and state validation.
    * So, we force the proper color here.  Not elegant, but it works.
    */
   {
      GLfloat colorSave[4];
      COPY_4V(colorSave, ctx->Current.Attrib[VERT_ATTRIB_COLOR0]);
      COPY_4V(ctx->Current.Attrib[VERT_ATTRIB_COLOR0], color);
      st_upload_constants(st, &st->fp->Base);
      COPY_4V(ctx->Current.Attrib[VERT_ATTRIB_COLOR0], colorSave);
   }

   cso_save_state(cso, (CSO_BIT_RASTERIZER |
                        CSO_BIT_FRAGMENT_SAMPLERS |
                        CSO_BIT_FRAGMENT_SAMPLER_VIEWS |
                        CSO_BIT_VIEWPORT |
                        CSO_BIT_STREAM_OUTPUTS |
                        CSO_BIT_VERTEX_ELEMENTS |
                        CSO_BIT_AUX_VERTEX_BUFFER_SLOT |
                        CSO_BITS_ALL_SHADERS));


   /* rasterizer state: just scissor */
   st->bitmap.rasterizer.scissor = ctx->Scissor.EnableFlags & 1;
   cso_set_rasterizer(cso, &st->bitmap.rasterizer);

   /* fragment shader state: TEX lookup program */
   cso_set_fragment_shader_handle(cso, fpv->driver_shader);

   /* vertex shader state: position + texcoord pass-through */
   cso_set_vertex_shader_handle(cso, st->bitmap.vs);

   /* disable other shaders */
   cso_set_tessctrl_shader_handle(cso, NULL);
   cso_set_tesseval_shader_handle(cso, NULL);
   cso_set_geometry_shader_handle(cso, NULL);

   /* user samplers, plus our bitmap sampler */
   {
      struct pipe_sampler_state *samplers[PIPE_MAX_SAMPLERS];
      uint num = MAX2(fpv->bitmap_sampler + 1,
                      st->state.num_frag_samplers);
      uint i;
      for (i = 0; i < st->state.num_frag_samplers; i++) {
         samplers[i] = &st->state.frag_samplers[i];
      }
      if (atlas)
         samplers[fpv->bitmap_sampler] = &st->bitmap.atlas_sampler;
      else
         samplers[fpv->bitmap_sampler] = &st->bitmap.sampler;
      cso_set_samplers(cso, PIPE_SHADER_FRAGMENT, num,
                       (const struct pipe_sampler_state **) samplers);
   }

   /* user textures, plus the bitmap texture */
   {
      struct pipe_sampler_view *sampler_views[PIPE_MAX_SAMPLERS];
      uint num = MAX2(fpv->bitmap_sampler + 1,
                      st->state.num_sampler_views[PIPE_SHADER_FRAGMENT]);
      memcpy(sampler_views, st->state.frag_sampler_views,
             sizeof(sampler_views));
      sampler_views[fpv->bitmap_sampler] = sv;
      cso_set_sampler_views(cso, PIPE_SHADER_FRAGMENT, num, sampler_views);
   }

   /* viewport state: viewport matching window dims */
   cso_set_viewport_dims(cso, st->state.fb_width,
                         st->state.fb_height,
                         st->state.fb_orientation == Y_0_TOP);

   cso_set_vertex_elements(cso, 3, st->util_velems);

   cso_set_stream_outputs(st->cso_context, 0, NULL, NULL);
}


/**
 * Restore pipeline state after rendering the bitmap textured quad.
 */
static void
restore_render_state(struct gl_context *ctx)
{
   struct st_context *st = st_context(ctx);
   struct cso_context *cso = st->cso_context;

   cso_restore_state(cso);
}


/**
 * Render a glBitmap by drawing a textured quad
 */
static void
draw_bitmap_quad(struct gl_context *ctx, GLint x, GLint y, GLfloat z,
                 GLsizei width, GLsizei height,
                 struct pipe_sampler_view *sv,
                 const GLfloat *color)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   const float fb_width = (float) st->state.fb_width;
   const float fb_height = (float) st->state.fb_height;
   const float x0 = (float) x;
   const float x1 = (float) (x + width);
   const float y0 = (float) y;
   const float y1 = (float) (y + height);
   float sLeft = 0.0f, sRight = 1.0f;
   float tTop = 0.0f, tBot = 1.0f - tTop;
   const float clip_x0 = x0 / fb_width * 2.0f - 1.0f;
   const float clip_y0 = y0 / fb_height * 2.0f - 1.0f;
   const float clip_x1 = x1 / fb_width * 2.0f - 1.0f;
   const float clip_y1 = y1 / fb_height * 2.0f - 1.0f;

   /* limit checks */
   {
      /* XXX if the bitmap is larger than the max texture size, break
       * it up into chunks.
       */
      GLuint MAYBE_UNUSED maxSize =
         1 << (pipe->screen->get_param(pipe->screen,
                                       PIPE_CAP_MAX_TEXTURE_2D_LEVELS) - 1);
      assert(width <= (GLsizei) maxSize);
      assert(height <= (GLsizei) maxSize);
   }

   setup_render_state(ctx, sv, color, false);

   /* convert Z from [0,1] to [-1,-1] to match viewport Z scale/bias */
   z = z * 2.0f - 1.0f;

   if (sv->texture->target == PIPE_TEXTURE_RECT) {
      /* use non-normalized texcoords */
      sRight = (float) width;
      tBot = (float) height;
   }

   if (!st_draw_quad(st, clip_x0, clip_y0, clip_x1, clip_y1, z,
                     sLeft, tBot, sRight, tTop, color, 0)) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glBitmap");
   }

   restore_render_state(ctx);

   /* We uploaded modified constants, need to invalidate them. */
   st->dirty |= ST_NEW_FS_CONSTANTS;
}


static void
reset_cache(struct st_context *st)
{
   struct st_bitmap_cache *cache = &st->bitmap.cache;

   /*memset(cache->buffer, 0xff, sizeof(cache->buffer));*/
   cache->empty = GL_TRUE;

   cache->xmin = 1000000;
   cache->xmax = -1000000;
   cache->ymin = 1000000;
   cache->ymax = -1000000;

   assert(!cache->texture);

   /* allocate a new texture */
   cache->texture = st_texture_create(st, st->internal_target,
                                      st->bitmap.tex_format, 0,
                                      BITMAP_CACHE_WIDTH, BITMAP_CACHE_HEIGHT,
                                      1, 1, 0,
				      PIPE_BIND_SAMPLER_VIEW);
}


/** Print bitmap image to stdout (debug) */
static void
print_cache(const struct st_bitmap_cache *cache)
{
   int i, j, k;

   for (i = 0; i < BITMAP_CACHE_HEIGHT; i++) {
      k = BITMAP_CACHE_WIDTH * (BITMAP_CACHE_HEIGHT - i - 1);
      for (j = 0; j < BITMAP_CACHE_WIDTH; j++) {
         if (cache->buffer[k])
            printf("X");
         else
            printf(" ");
         k++;
      }
      printf("\n");
   }
}


/**
 * Create gallium pipe_transfer object for the bitmap cache.
 */
static void
create_cache_trans(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;
   struct st_bitmap_cache *cache = &st->bitmap.cache;

   if (cache->trans)
      return;

   /* Map the texture transfer.
    * Subsequent glBitmap calls will write into the texture image.
    */
   cache->buffer = pipe_transfer_map(pipe, cache->texture, 0, 0,
                                     PIPE_TRANSFER_WRITE, 0, 0,
                                     BITMAP_CACHE_WIDTH,
                                     BITMAP_CACHE_HEIGHT, &cache->trans);

   /* init image to all 0xff */
   memset(cache->buffer, 0xff, cache->trans->stride * BITMAP_CACHE_HEIGHT);
}


/**
 * If there's anything in the bitmap cache, draw/flush it now.
 */
void
st_flush_bitmap_cache(struct st_context *st)
{
   struct st_bitmap_cache *cache = &st->bitmap.cache;

   if (!cache->empty) {
      struct pipe_context *pipe = st->pipe;
      struct pipe_sampler_view *sv;

      assert(cache->xmin <= cache->xmax);

      if (0)
         printf("flush bitmap, size %d x %d  at %d, %d\n",
                cache->xmax - cache->xmin,
                cache->ymax - cache->ymin,
                cache->xpos, cache->ypos);

      /* The texture transfer has been mapped until now.
       * So unmap and release the texture transfer before drawing.
       */
      if (cache->trans && cache->buffer) {
         if (0)
            print_cache(cache);
         pipe_transfer_unmap(pipe, cache->trans);
         cache->buffer = NULL;
         cache->trans = NULL;
      }

      sv = st_create_texture_sampler_view(st->pipe, cache->texture);
      if (sv) {
         draw_bitmap_quad(st->ctx,
                          cache->xpos,
                          cache->ypos,
                          cache->zpos,
                          BITMAP_CACHE_WIDTH, BITMAP_CACHE_HEIGHT,
                          sv,
                          cache->color);

         pipe_sampler_view_reference(&sv, NULL);
      }

      /* release/free the texture */
      pipe_resource_reference(&cache->texture, NULL);

      reset_cache(st);
   }
}


/**
 * Try to accumulate this glBitmap call in the bitmap cache.
 * \return  GL_TRUE for success, GL_FALSE if bitmap is too large, etc.
 */
static GLboolean
accum_bitmap(struct gl_context *ctx,
             GLint x, GLint y, GLsizei width, GLsizei height,
             const struct gl_pixelstore_attrib *unpack,
             const GLubyte *bitmap )
{
   struct st_context *st = ctx->st;
   struct st_bitmap_cache *cache = &st->bitmap.cache;
   int px = -999, py = -999;
   const GLfloat z = ctx->Current.RasterPos[2];

   if (width > BITMAP_CACHE_WIDTH ||
       height > BITMAP_CACHE_HEIGHT)
      return GL_FALSE; /* too big to cache */

   if (!cache->empty) {
      px = x - cache->xpos;  /* pos in buffer */
      py = y - cache->ypos;
      if (px < 0 || px + width > BITMAP_CACHE_WIDTH ||
          py < 0 || py + height > BITMAP_CACHE_HEIGHT ||
          !TEST_EQ_4V(ctx->Current.RasterColor, cache->color) ||
          ((fabs(z - cache->zpos) > Z_EPSILON))) {
         /* This bitmap would extend beyond cache bounds, or the bitmap
          * color is changing
          * so flush and continue.
          */
         st_flush_bitmap_cache(st);
      }
   }

   if (cache->empty) {
      /* Initialize.  Center bitmap vertically in the buffer. */
      px = 0;
      py = (BITMAP_CACHE_HEIGHT - height) / 2;
      cache->xpos = x;
      cache->ypos = y - py;
      cache->zpos = z;
      cache->empty = GL_FALSE;
      COPY_4FV(cache->color, ctx->Current.RasterColor);
   }

   assert(px != -999);
   assert(py != -999);

   if (x < cache->xmin)
      cache->xmin = x;
   if (y < cache->ymin)
      cache->ymin = y;
   if (x + width > cache->xmax)
      cache->xmax = x + width;
   if (y + height > cache->ymax)
      cache->ymax = y + height;

   /* create the transfer if needed */
   create_cache_trans(st);

   /* PBO source... */
   bitmap = _mesa_map_pbo_source(ctx, unpack, bitmap);
   if (!bitmap) {
      return FALSE;
   }

   unpack_bitmap(st, px, py, width, height, unpack, bitmap,
                 cache->buffer, BITMAP_CACHE_WIDTH);

   _mesa_unmap_pbo_source(ctx, unpack);

   return GL_TRUE; /* accumulated */
}


/**
 * One-time init for drawing bitmaps.
 */
static void
init_bitmap_state(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;
   struct pipe_screen *screen = pipe->screen;

   /* This function should only be called once */
   assert(st->bitmap.vs == NULL);

   assert(st->internal_target == PIPE_TEXTURE_2D ||
          st->internal_target == PIPE_TEXTURE_RECT);

   /* init sampler state once */
   memset(&st->bitmap.sampler, 0, sizeof(st->bitmap.sampler));
   st->bitmap.sampler.wrap_s = PIPE_TEX_WRAP_CLAMP;
   st->bitmap.sampler.wrap_t = PIPE_TEX_WRAP_CLAMP;
   st->bitmap.sampler.wrap_r = PIPE_TEX_WRAP_CLAMP;
   st->bitmap.sampler.min_img_filter = PIPE_TEX_FILTER_NEAREST;
   st->bitmap.sampler.min_mip_filter = PIPE_TEX_MIPFILTER_NONE;
   st->bitmap.sampler.mag_img_filter = PIPE_TEX_FILTER_NEAREST;
   st->bitmap.sampler.normalized_coords = st->internal_target == PIPE_TEXTURE_2D;

   st->bitmap.atlas_sampler = st->bitmap.sampler;
   st->bitmap.atlas_sampler.normalized_coords = 0;

   /* init baseline rasterizer state once */
   memset(&st->bitmap.rasterizer, 0, sizeof(st->bitmap.rasterizer));
   st->bitmap.rasterizer.half_pixel_center = 1;
   st->bitmap.rasterizer.bottom_edge_rule = 1;
   st->bitmap.rasterizer.depth_clip = 1;

   /* find a usable texture format */
   if (screen->is_format_supported(screen, PIPE_FORMAT_I8_UNORM,
                                   st->internal_target, 0,
                                   PIPE_BIND_SAMPLER_VIEW)) {
      st->bitmap.tex_format = PIPE_FORMAT_I8_UNORM;
   }
   else if (screen->is_format_supported(screen, PIPE_FORMAT_A8_UNORM,
                                        st->internal_target, 0,
                                        PIPE_BIND_SAMPLER_VIEW)) {
      st->bitmap.tex_format = PIPE_FORMAT_A8_UNORM;
   }
   else if (screen->is_format_supported(screen, PIPE_FORMAT_L8_UNORM,
                                        st->internal_target, 0,
                                        PIPE_BIND_SAMPLER_VIEW)) {
      st->bitmap.tex_format = PIPE_FORMAT_L8_UNORM;
   }
   else {
      /* XXX support more formats */
      assert(0);
   }

   /* Create the vertex shader */
   {
      const uint semantic_names[] = { TGSI_SEMANTIC_POSITION,
                                      TGSI_SEMANTIC_COLOR,
        st->needs_texcoord_semantic ? TGSI_SEMANTIC_TEXCOORD :
                                      TGSI_SEMANTIC_GENERIC };
      const uint semantic_indexes[] = { 0, 0, 0 };
      st->bitmap.vs = util_make_vertex_passthrough_shader(st->pipe, 3,
                                                          semantic_names,
                                                          semantic_indexes,
                                                          FALSE);
   }

   reset_cache(st);
}


/**
 * Called via ctx->Driver.Bitmap()
 */
static void
st_Bitmap(struct gl_context *ctx, GLint x, GLint y,
          GLsizei width, GLsizei height,
          const struct gl_pixelstore_attrib *unpack, const GLubyte *bitmap )
{
   struct st_context *st = st_context(ctx);
   struct pipe_resource *pt;

   assert(width > 0);
   assert(height > 0);

   st_invalidate_readpix_cache(st);

   if (!st->bitmap.vs) {
      init_bitmap_state(st);
   }

   /* We only need to validate any non-ST_NEW_CONSTANTS state. The VS we use
    * for bitmap drawing uses no constants and the FS constants are
    * explicitly uploaded in the draw_bitmap_quad() function.
    */
   if ((st->dirty | ctx->NewDriverState) & ~ST_NEW_CONSTANTS &
       ST_PIPELINE_RENDER_STATE_MASK ||
       st->gfx_shaders_may_be_dirty) {
      st_validate_state(st, ST_PIPELINE_META);
   }

   if (UseBitmapCache && accum_bitmap(ctx, x, y, width, height, unpack, bitmap))
      return;

   pt = make_bitmap_texture(ctx, width, height, unpack, bitmap);
   if (pt) {
      struct pipe_sampler_view *sv =
         st_create_texture_sampler_view(st->pipe, pt);

      assert(pt->target == PIPE_TEXTURE_2D || pt->target == PIPE_TEXTURE_RECT);

      if (sv) {
         draw_bitmap_quad(ctx, x, y, ctx->Current.RasterPos[2],
                          width, height, sv, ctx->Current.RasterColor);

         pipe_sampler_view_reference(&sv, NULL);
      }

      /* release/free the texture */
      pipe_resource_reference(&pt, NULL);
   }
}


/**
 * Called via ctx->Driver.DrawAtlasBitmap()
 */
static void
st_DrawAtlasBitmaps(struct gl_context *ctx,
                    const struct gl_bitmap_atlas *atlas,
                    GLuint count, const GLubyte *ids)
{
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct st_texture_object *stObj = st_texture_object(atlas->texObj);
   struct pipe_sampler_view *sv;
   /* convert Z from [0,1] to [-1,-1] to match viewport Z scale/bias */
   const float z = ctx->Current.RasterPos[2] * 2.0f - 1.0f;
   const float *color = ctx->Current.RasterColor;
   const float clip_x_scale = 2.0f / st->state.fb_width;
   const float clip_y_scale = 2.0f / st->state.fb_height;
   const unsigned num_verts = count * 4;
   const unsigned num_vert_bytes = num_verts * sizeof(struct st_util_vertex);
   struct st_util_vertex *verts;
   struct pipe_vertex_buffer vb = {0};
   unsigned i;

   if (!st->bitmap.vs) {
      init_bitmap_state(st);
   }

   st_flush_bitmap_cache(st);

   st_validate_state(st, ST_PIPELINE_META);
   st_invalidate_readpix_cache(st);

   sv = st_create_texture_sampler_view(pipe, stObj->pt);
   if (!sv) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glCallLists(bitmap text)");
      return;
   }

   setup_render_state(ctx, sv, color, true);

   vb.stride = sizeof(struct st_util_vertex);

   u_upload_alloc(pipe->stream_uploader, 0, num_vert_bytes, 4,
                  &vb.buffer_offset, &vb.buffer.resource, (void **) &verts);

   if (unlikely(!verts)) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glCallLists(bitmap text)");
      goto out;
   }

   /* build quads vertex data */
   for (i = 0; i < count; i++) {
      const GLfloat epsilon = 0.0001F;
      const struct gl_bitmap_glyph *g = &atlas->glyphs[ids[i]];
      const float xmove = g->xmove, ymove = g->ymove;
      const float xorig = g->xorig, yorig = g->yorig;
      const float s0 = g->x, t0 = g->y;
      const float s1 = s0 + g->w, t1 = t0 + g->h;
      const float x0 = IFLOOR(ctx->Current.RasterPos[0] - xorig + epsilon);
      const float y0 = IFLOOR(ctx->Current.RasterPos[1] - yorig + epsilon);
      const float x1 = x0 + g->w, y1 = y0 + g->h;
      const float clip_x0 = x0 * clip_x_scale - 1.0f;
      const float clip_y0 = y0 * clip_y_scale - 1.0f;
      const float clip_x1 = x1 * clip_x_scale - 1.0f;
      const float clip_y1 = y1 * clip_y_scale - 1.0f;

      /* lower-left corner */
      verts->x = clip_x0;
      verts->y = clip_y0;
      verts->z = z;
      verts->r = color[0];
      verts->g = color[1];
      verts->b = color[2];
      verts->a = color[3];
      verts->s = s0;
      verts->t = t0;
      verts++;

      /* lower-right corner */
      verts->x = clip_x1;
      verts->y = clip_y0;
      verts->z = z;
      verts->r = color[0];
      verts->g = color[1];
      verts->b = color[2];
      verts->a = color[3];
      verts->s = s1;
      verts->t = t0;
      verts++;

      /* upper-right corner */
      verts->x = clip_x1;
      verts->y = clip_y1;
      verts->z = z;
      verts->r = color[0];
      verts->g = color[1];
      verts->b = color[2];
      verts->a = color[3];
      verts->s = s1;
      verts->t = t1;
      verts++;

      /* upper-left corner */
      verts->x = clip_x0;
      verts->y = clip_y1;
      verts->z = z;
      verts->r = color[0];
      verts->g = color[1];
      verts->b = color[2];
      verts->a = color[3];
      verts->s = s0;
      verts->t = t1;
      verts++;

      /* Update the raster position */
      ctx->Current.RasterPos[0] += xmove;
      ctx->Current.RasterPos[1] += ymove;
   }

   u_upload_unmap(pipe->stream_uploader);

   cso_set_vertex_buffers(st->cso_context,
                          cso_get_aux_vertex_buffer_slot(st->cso_context),
                          1, &vb);

   cso_draw_arrays(st->cso_context, PIPE_PRIM_QUADS, 0, num_verts);

out:
   restore_render_state(ctx);

   pipe_resource_reference(&vb.buffer.resource, NULL);

   pipe_sampler_view_reference(&sv, NULL);

   /* We uploaded modified constants, need to invalidate them. */
   st->dirty |= ST_NEW_FS_CONSTANTS;
}



/** Per-context init */
void
st_init_bitmap_functions(struct dd_function_table *functions)
{
   functions->Bitmap = st_Bitmap;
   functions->DrawAtlasBitmaps = st_DrawAtlasBitmaps;
}


/** Per-context tear-down */
void
st_destroy_bitmap(struct st_context *st)
{
   struct pipe_context *pipe = st->pipe;
   struct st_bitmap_cache *cache = &st->bitmap.cache;

   if (st->bitmap.vs) {
      cso_delete_vertex_shader(st->cso_context, st->bitmap.vs);
      st->bitmap.vs = NULL;
   }

   if (cache->trans && cache->buffer) {
      pipe_transfer_unmap(pipe, cache->trans);
   }
   pipe_resource_reference(&st->bitmap.cache.texture, NULL);
}
