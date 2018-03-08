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

#include "main/mtypes.h"
#include "main/extensions.h"
#include "main/context.h"
#include "main/debug_output.h"
#include "main/glthread.h"
#include "main/texobj.h"
#include "main/teximage.h"
#include "main/texstate.h"
#include "main/errors.h"
#include "main/framebuffer.h"
#include "main/fbobject.h"
#include "main/renderbuffer.h"
#include "main/version.h"
#include "util/hash_table.h"
#include "st_texture.h"

#include "st_context.h"
#include "st_debug.h"
#include "st_extensions.h"
#include "st_format.h"
#include "st_cb_bitmap.h"
#include "st_cb_fbo.h"
#include "st_cb_flush.h"
#include "st_manager.h"
#include "st_sampler_view.h"

#include "state_tracker/st_gl_api.h"

#include "pipe/p_context.h"
#include "pipe/p_screen.h"
#include "util/u_format.h"
#include "util/u_pointer.h"
#include "util/u_inlines.h"
#include "util/u_atomic.h"
#include "util/u_surface.h"
#include "util/list.h"

struct hash_table;
struct st_manager_private
{
   struct hash_table *stfbi_ht; /* framebuffer iface objects hash table */
   mtx_t st_mutex;
};


/**
 * Map an attachment to a buffer index.
 */
static inline gl_buffer_index
attachment_to_buffer_index(enum st_attachment_type statt)
{
   gl_buffer_index index;

   switch (statt) {
   case ST_ATTACHMENT_FRONT_LEFT:
      index = BUFFER_FRONT_LEFT;
      break;
   case ST_ATTACHMENT_BACK_LEFT:
      index = BUFFER_BACK_LEFT;
      break;
   case ST_ATTACHMENT_FRONT_RIGHT:
      index = BUFFER_FRONT_RIGHT;
      break;
   case ST_ATTACHMENT_BACK_RIGHT:
      index = BUFFER_BACK_RIGHT;
      break;
   case ST_ATTACHMENT_DEPTH_STENCIL:
      index = BUFFER_DEPTH;
      break;
   case ST_ATTACHMENT_ACCUM:
      index = BUFFER_ACCUM;
      break;
   case ST_ATTACHMENT_SAMPLE:
   default:
      index = BUFFER_COUNT;
      break;
   }

   return index;
}


/**
 * Map a buffer index to an attachment.
 */
static inline enum st_attachment_type
buffer_index_to_attachment(gl_buffer_index index)
{
   enum st_attachment_type statt;

   switch (index) {
   case BUFFER_FRONT_LEFT:
      statt = ST_ATTACHMENT_FRONT_LEFT;
      break;
   case BUFFER_BACK_LEFT:
      statt = ST_ATTACHMENT_BACK_LEFT;
      break;
   case BUFFER_FRONT_RIGHT:
      statt = ST_ATTACHMENT_FRONT_RIGHT;
      break;
   case BUFFER_BACK_RIGHT:
      statt = ST_ATTACHMENT_BACK_RIGHT;
      break;
   case BUFFER_DEPTH:
      statt = ST_ATTACHMENT_DEPTH_STENCIL;
      break;
   case BUFFER_ACCUM:
      statt = ST_ATTACHMENT_ACCUM;
      break;
   default:
      statt = ST_ATTACHMENT_INVALID;
      break;
   }

   return statt;
}


/**
 * Make sure a context picks up the latest cached state of the
 * drawables it binds to.
 */
static void
st_context_validate(struct st_context *st,
                    struct st_framebuffer *stdraw,
                    struct st_framebuffer *stread)
{
    if (stdraw && stdraw->stamp != st->draw_stamp) {
       st->dirty |= ST_NEW_FRAMEBUFFER;
       _mesa_resize_framebuffer(st->ctx, &stdraw->Base,
                                stdraw->Base.Width,
                                stdraw->Base.Height);
       st->draw_stamp = stdraw->stamp;
    }

    if (stread && stread->stamp != st->read_stamp) {
       if (stread != stdraw) {
          st->dirty |= ST_NEW_FRAMEBUFFER;
          _mesa_resize_framebuffer(st->ctx, &stread->Base,
                                   stread->Base.Width,
                                   stread->Base.Height);
       }
       st->read_stamp = stread->stamp;
    }
}


/**
 * Validate a framebuffer to make sure up-to-date pipe_textures are used.
 * The context is only used for creating pipe surfaces and for calling
 * _mesa_resize_framebuffer().
 * (That should probably be rethought, since those surfaces become
 * drawable state, not context state, and can be freed by another pipe
 * context).
 */
static void
st_framebuffer_validate(struct st_framebuffer *stfb,
                        struct st_context *st)
{
   struct pipe_resource *textures[ST_ATTACHMENT_COUNT];
   uint width, height;
   unsigned i;
   boolean changed = FALSE;
   int32_t new_stamp;

   new_stamp = p_atomic_read(&stfb->iface->stamp);
   if (stfb->iface_stamp == new_stamp)
      return;

   memset(textures, 0, stfb->num_statts * sizeof(textures[0]));

   /* validate the fb */
   do {
      if (!stfb->iface->validate(&st->iface, stfb->iface, stfb->statts,
                                 stfb->num_statts, textures))
         return;

      stfb->iface_stamp = new_stamp;
      new_stamp = p_atomic_read(&stfb->iface->stamp);
   } while(stfb->iface_stamp != new_stamp);

   width = stfb->Base.Width;
   height = stfb->Base.Height;

   for (i = 0; i < stfb->num_statts; i++) {
      struct st_renderbuffer *strb;
      struct pipe_surface *ps, surf_tmpl;
      gl_buffer_index idx;

      if (!textures[i])
         continue;

      idx = attachment_to_buffer_index(stfb->statts[i]);
      if (idx >= BUFFER_COUNT) {
         pipe_resource_reference(&textures[i], NULL);
         continue;
      }

      strb = st_renderbuffer(stfb->Base.Attachment[idx].Renderbuffer);
      assert(strb);
      if (strb->texture == textures[i]) {
         pipe_resource_reference(&textures[i], NULL);
         continue;
      }

      u_surface_default_template(&surf_tmpl, textures[i]);
      ps = st->pipe->create_surface(st->pipe, textures[i], &surf_tmpl);
      if (ps) {
         struct pipe_surface **psurf =
            util_format_is_srgb(ps->format) ? &strb->surface_srgb :
                                              &strb->surface_linear;

         pipe_surface_reference(psurf, ps);
         strb->surface = *psurf;
         pipe_resource_reference(&strb->texture, ps->texture);
         /* ownership transfered */
         pipe_surface_reference(&ps, NULL);

         changed = TRUE;

         strb->Base.Width = strb->surface->width;
         strb->Base.Height = strb->surface->height;

         width = strb->Base.Width;
         height = strb->Base.Height;
      }

      pipe_resource_reference(&textures[i], NULL);
   }

   if (changed) {
      ++stfb->stamp;
      _mesa_resize_framebuffer(st->ctx, &stfb->Base, width, height);
   }
}


/**
 * Update the attachments to validate by looping the existing renderbuffers.
 */
static void
st_framebuffer_update_attachments(struct st_framebuffer *stfb)
{
   gl_buffer_index idx;

   stfb->num_statts = 0;
   for (idx = 0; idx < BUFFER_COUNT; idx++) {
      struct st_renderbuffer *strb;
      enum st_attachment_type statt;

      strb = st_renderbuffer(stfb->Base.Attachment[idx].Renderbuffer);
      if (!strb || strb->software)
         continue;

      statt = buffer_index_to_attachment(idx);
      if (statt != ST_ATTACHMENT_INVALID &&
          st_visual_have_buffers(stfb->iface->visual, 1 << statt))
         stfb->statts[stfb->num_statts++] = statt;
   }
   stfb->stamp++;
}


/**
 * Add a renderbuffer to the framebuffer.  The framebuffer is one that
 * corresponds to a window and is not a user-created FBO.
 */
static boolean
st_framebuffer_add_renderbuffer(struct st_framebuffer *stfb,
                                gl_buffer_index idx)
{
   struct gl_renderbuffer *rb;
   enum pipe_format format;
   boolean sw;

   assert(_mesa_is_winsys_fbo(&stfb->Base));

   /* do not distinguish depth/stencil buffers */
   if (idx == BUFFER_STENCIL)
      idx = BUFFER_DEPTH;

   switch (idx) {
   case BUFFER_DEPTH:
      format = stfb->iface->visual->depth_stencil_format;
      sw = FALSE;
      break;
   case BUFFER_ACCUM:
      format = stfb->iface->visual->accum_format;
      sw = TRUE;
      break;
   default:
      format = stfb->iface->visual->color_format;
      if (stfb->Base.Visual.sRGBCapable)
         format = util_format_srgb(format);
      sw = FALSE;
      break;
   }

   if (format == PIPE_FORMAT_NONE)
      return FALSE;

   rb = st_new_renderbuffer_fb(format, stfb->iface->visual->samples, sw);
   if (!rb)
      return FALSE;

   if (idx != BUFFER_DEPTH) {
      _mesa_attach_and_own_rb(&stfb->Base, idx, rb);
      return TRUE;
   }

   bool rb_ownership_taken = false;
   if (util_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_ZS, 0)) {
      _mesa_attach_and_own_rb(&stfb->Base, BUFFER_DEPTH, rb);
      rb_ownership_taken = true;
   }

   if (util_format_get_component_bits(format, UTIL_FORMAT_COLORSPACE_ZS, 1)) {
      if (rb_ownership_taken)
         _mesa_attach_and_reference_rb(&stfb->Base, BUFFER_STENCIL, rb);
      else
         _mesa_attach_and_own_rb(&stfb->Base, BUFFER_STENCIL, rb);
   }

   return TRUE;
}


/**
 * Intialize a struct gl_config from a visual.
 */
static void
st_visual_to_context_mode(const struct st_visual *visual,
                          struct gl_config *mode)
{
   memset(mode, 0, sizeof(*mode));

   if (st_visual_have_buffers(visual, ST_ATTACHMENT_BACK_LEFT_MASK))
      mode->doubleBufferMode = GL_TRUE;

   if (st_visual_have_buffers(visual,
            ST_ATTACHMENT_FRONT_RIGHT_MASK | ST_ATTACHMENT_BACK_RIGHT_MASK))
      mode->stereoMode = GL_TRUE;

   if (visual->color_format != PIPE_FORMAT_NONE) {
      mode->rgbMode = GL_TRUE;

      mode->redBits =
         util_format_get_component_bits(visual->color_format,
               UTIL_FORMAT_COLORSPACE_RGB, 0);
      mode->greenBits =
         util_format_get_component_bits(visual->color_format,
               UTIL_FORMAT_COLORSPACE_RGB, 1);
      mode->blueBits =
         util_format_get_component_bits(visual->color_format,
               UTIL_FORMAT_COLORSPACE_RGB, 2);
      mode->alphaBits =
         util_format_get_component_bits(visual->color_format,
               UTIL_FORMAT_COLORSPACE_RGB, 3);

      mode->rgbBits = mode->redBits +
         mode->greenBits + mode->blueBits + mode->alphaBits;
      mode->sRGBCapable = util_format_is_srgb(visual->color_format);
   }

   if (visual->depth_stencil_format != PIPE_FORMAT_NONE) {
      mode->depthBits =
         util_format_get_component_bits(visual->depth_stencil_format,
               UTIL_FORMAT_COLORSPACE_ZS, 0);
      mode->stencilBits =
         util_format_get_component_bits(visual->depth_stencil_format,
               UTIL_FORMAT_COLORSPACE_ZS, 1);

      mode->haveDepthBuffer = mode->depthBits > 0;
      mode->haveStencilBuffer = mode->stencilBits > 0;
   }

   if (visual->accum_format != PIPE_FORMAT_NONE) {
      mode->haveAccumBuffer = GL_TRUE;

      mode->accumRedBits =
         util_format_get_component_bits(visual->accum_format,
               UTIL_FORMAT_COLORSPACE_RGB, 0);
      mode->accumGreenBits =
         util_format_get_component_bits(visual->accum_format,
               UTIL_FORMAT_COLORSPACE_RGB, 1);
      mode->accumBlueBits =
         util_format_get_component_bits(visual->accum_format,
               UTIL_FORMAT_COLORSPACE_RGB, 2);
      mode->accumAlphaBits =
         util_format_get_component_bits(visual->accum_format,
               UTIL_FORMAT_COLORSPACE_RGB, 3);
   }

   if (visual->samples > 1) {
      mode->sampleBuffers = 1;
      mode->samples = visual->samples;
   }
}


/**
 * Create a framebuffer from a manager interface.
 */
static struct st_framebuffer *
st_framebuffer_create(struct st_context *st,
                      struct st_framebuffer_iface *stfbi)
{
   struct st_framebuffer *stfb;
   struct gl_config mode;
   gl_buffer_index idx;

   if (!stfbi)
      return NULL;

   stfb = CALLOC_STRUCT(st_framebuffer);
   if (!stfb)
      return NULL;

   st_visual_to_context_mode(stfbi->visual, &mode);

   /*
    * For desktop GL, sRGB framebuffer write is controlled by both the
    * capability of the framebuffer and GL_FRAMEBUFFER_SRGB.  We should
    * advertise the capability when the pipe driver (and core Mesa) supports
    * it so that applications can enable sRGB write when they want to.
    *
    * This is not to be confused with GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB.  When
    * the attribute is GLX_TRUE, it tells the st manager to pick a color
    * format such that util_format_srgb(visual->color_format) can be supported
    * by the pipe driver.  We still need to advertise the capability here.
    *
    * For GLES, however, sRGB framebuffer write is controlled only by the
    * capability of the framebuffer.  There is GL_EXT_sRGB_write_control to
    * give applications the control back, but sRGB write is still enabled by
    * default.  To avoid unexpected results, we should not advertise the
    * capability.  This could change when we add support for
    * EGL_KHR_gl_colorspace.
    */
   if (_mesa_is_desktop_gl(st->ctx)) {
      struct pipe_screen *screen = st->pipe->screen;
      const enum pipe_format srgb_format =
         util_format_srgb(stfbi->visual->color_format);

      if (srgb_format != PIPE_FORMAT_NONE &&
          st_pipe_format_to_mesa_format(srgb_format) != MESA_FORMAT_NONE &&
          screen->is_format_supported(screen, srgb_format,
                                      PIPE_TEXTURE_2D, stfbi->visual->samples,
                                      (PIPE_BIND_DISPLAY_TARGET |
                                       PIPE_BIND_RENDER_TARGET)))
         mode.sRGBCapable = GL_TRUE;
   }

   _mesa_initialize_window_framebuffer(&stfb->Base, &mode);

   stfb->iface = stfbi;
   stfb->iface_ID = stfbi->ID;
   stfb->iface_stamp = p_atomic_read(&stfbi->stamp) - 1;

   /* add the color buffer */
   idx = stfb->Base._ColorDrawBufferIndexes[0];
   if (!st_framebuffer_add_renderbuffer(stfb, idx)) {
      free(stfb);
      return NULL;
   }

   st_framebuffer_add_renderbuffer(stfb, BUFFER_DEPTH);
   st_framebuffer_add_renderbuffer(stfb, BUFFER_ACCUM);

   stfb->stamp = 0;
   st_framebuffer_update_attachments(stfb);

   return stfb;
}


/**
 * Reference a framebuffer.
 */
void
st_framebuffer_reference(struct st_framebuffer **ptr,
                         struct st_framebuffer *stfb)
{
   struct gl_framebuffer *fb = &stfb->Base;
   _mesa_reference_framebuffer((struct gl_framebuffer **) ptr, fb);
}


static uint32_t
st_framebuffer_iface_hash(const void *key)
{
   return (uintptr_t)key;
}


static bool
st_framebuffer_iface_equal(const void *a, const void *b)
{
   return (struct st_framebuffer_iface *)a == (struct st_framebuffer_iface *)b;
}


static boolean
st_framebuffer_iface_lookup(struct st_manager *smapi,
                            const struct st_framebuffer_iface *stfbi)
{
   struct st_manager_private *smPriv =
      (struct st_manager_private *)smapi->st_manager_private;
   struct hash_entry *entry;

   assert(smPriv);
   assert(smPriv->stfbi_ht);

   mtx_lock(&smPriv->st_mutex);
   entry = _mesa_hash_table_search(smPriv->stfbi_ht, stfbi);
   mtx_unlock(&smPriv->st_mutex);

   return entry != NULL;
}


static boolean
st_framebuffer_iface_insert(struct st_manager *smapi,
                            struct st_framebuffer_iface *stfbi)
{
   struct st_manager_private *smPriv =
      (struct st_manager_private *)smapi->st_manager_private;
   struct hash_entry *entry;

   assert(smPriv);
   assert(smPriv->stfbi_ht);

   mtx_lock(&smPriv->st_mutex);
   entry = _mesa_hash_table_insert(smPriv->stfbi_ht, stfbi, stfbi);
   mtx_unlock(&smPriv->st_mutex);

   return entry != NULL;
}


static void
st_framebuffer_iface_remove(struct st_manager *smapi,
                            struct st_framebuffer_iface *stfbi)
{
   struct st_manager_private *smPriv =
      (struct st_manager_private *)smapi->st_manager_private;
   struct hash_entry *entry;

   if (!smPriv || !smPriv->stfbi_ht)
      return;

   mtx_lock(&smPriv->st_mutex);
   entry = _mesa_hash_table_search(smPriv->stfbi_ht, stfbi);
   if (!entry)
      goto unlock;

   _mesa_hash_table_remove(smPriv->stfbi_ht, entry);

unlock:
   mtx_unlock(&smPriv->st_mutex);
}


/**
 * The framebuffer interface object is no longer valid.
 * Remove the object from the framebuffer interface hash table.
 */
static void
st_api_destroy_drawable(struct st_api *stapi,
                        struct st_framebuffer_iface *stfbi)
{
   if (!stfbi)
      return;

   st_framebuffer_iface_remove(stfbi->state_manager, stfbi);
}


/**
 * Purge the winsys buffers list to remove any references to
 * non-existing framebuffer interface objects.
 */
static void
st_framebuffers_purge(struct st_context *st)
{
   struct st_context_iface *st_iface = &st->iface;
   struct st_manager *smapi = st_iface->state_manager;
   struct st_framebuffer *stfb, *next;

   assert(smapi);

   LIST_FOR_EACH_ENTRY_SAFE_REV(stfb, next, &st->winsys_buffers, head) {
      struct st_framebuffer_iface *stfbi = stfb->iface;

      assert(stfbi);

      /**
       * If the corresponding framebuffer interface object no longer exists,
       * remove the framebuffer object from the context's winsys buffers list,
       * and unreference the framebuffer object, so its resources can be
       * deleted.
       */
      if (!st_framebuffer_iface_lookup(smapi, stfbi)) {
         LIST_DEL(&stfb->head);
         st_framebuffer_reference(&stfb, NULL);
      }
   }
}


static void
st_context_flush(struct st_context_iface *stctxi, unsigned flags,
                 struct pipe_fence_handle **fence)
{
   struct st_context *st = (struct st_context *) stctxi;
   unsigned pipe_flags = 0;

   if (flags & ST_FLUSH_END_OF_FRAME)
      pipe_flags |= PIPE_FLUSH_END_OF_FRAME;
   if (flags & ST_FLUSH_FENCE_FD)
      pipe_flags |= PIPE_FLUSH_FENCE_FD;

   FLUSH_VERTICES(st->ctx, 0);
   FLUSH_CURRENT(st->ctx, 0);
   st_flush(st, fence, pipe_flags);

   if ((flags & ST_FLUSH_WAIT) && fence && *fence) {
      st->pipe->screen->fence_finish(st->pipe->screen, NULL, *fence,
                                     PIPE_TIMEOUT_INFINITE);
      st->pipe->screen->fence_reference(st->pipe->screen, fence, NULL);
   }

   if (flags & ST_FLUSH_FRONT)
      st_manager_flush_frontbuffer(st);

   /* DRI3 changes the framebuffer after SwapBuffers, but we need to invoke
    * st_manager_validate_framebuffers to notice that.
    *
    * Set gfx_shaders_may_be_dirty to invoke st_validate_state in the next
    * draw call, which will invoke st_manager_validate_framebuffers, but it
    * won't dirty states if there is no change.
    */
   if (flags & ST_FLUSH_END_OF_FRAME)
      st->gfx_shaders_may_be_dirty = true;
}

static boolean
st_context_teximage(struct st_context_iface *stctxi,
                    enum st_texture_type tex_type,
                    int level, enum pipe_format pipe_format,
                    struct pipe_resource *tex, boolean mipmap)
{
   struct st_context *st = (struct st_context *) stctxi;
   struct gl_context *ctx = st->ctx;
   struct gl_texture_object *texObj;
   struct gl_texture_image *texImage;
   struct st_texture_object *stObj;
   struct st_texture_image *stImage;
   GLenum internalFormat;
   GLuint width, height, depth;
   GLenum target;

   switch (tex_type) {
   case ST_TEXTURE_1D:
      target = GL_TEXTURE_1D;
      break;
   case ST_TEXTURE_2D:
      target = GL_TEXTURE_2D;
      break;
   case ST_TEXTURE_3D:
      target = GL_TEXTURE_3D;
      break;
   case ST_TEXTURE_RECT:
      target = GL_TEXTURE_RECTANGLE_ARB;
      break;
   default:
      return FALSE;
   }

   texObj = _mesa_get_current_tex_object(ctx, target);

   _mesa_lock_texture(ctx, texObj);

   stObj = st_texture_object(texObj);
   /* switch to surface based */
   if (!stObj->surface_based) {
      _mesa_clear_texture_object(ctx, texObj, NULL);
      stObj->surface_based = GL_TRUE;
   }

   texImage = _mesa_get_tex_image(ctx, texObj, target, level);
   stImage = st_texture_image(texImage);
   if (tex) {
      mesa_format texFormat = st_pipe_format_to_mesa_format(pipe_format);

      if (util_format_has_alpha(tex->format))
         internalFormat = GL_RGBA;
      else
         internalFormat = GL_RGB;

      _mesa_init_teximage_fields(ctx, texImage,
                                 tex->width0, tex->height0, 1, 0,
                                 internalFormat, texFormat);

      width = tex->width0;
      height = tex->height0;
      depth = tex->depth0;

      /* grow the image size until we hit level = 0 */
      while (level > 0) {
         if (width != 1)
            width <<= 1;
         if (height != 1)
            height <<= 1;
         if (depth != 1)
            depth <<= 1;
         level--;
      }
   }
   else {
      _mesa_clear_texture_image(ctx, texImage);
      width = height = depth = 0;
   }

   pipe_resource_reference(&stObj->pt, tex);
   st_texture_release_all_sampler_views(st, stObj);
   pipe_resource_reference(&stImage->pt, tex);
   stObj->surface_format = pipe_format;

   stObj->needs_validation = true;

   _mesa_dirty_texobj(ctx, texObj);
   _mesa_unlock_texture(ctx, texObj);

   return TRUE;
}


static void
st_context_copy(struct st_context_iface *stctxi,
                struct st_context_iface *stsrci, unsigned mask)
{
   struct st_context *st = (struct st_context *) stctxi;
   struct st_context *src = (struct st_context *) stsrci;

   _mesa_copy_context(src->ctx, st->ctx, mask);
}


static boolean
st_context_share(struct st_context_iface *stctxi,
                 struct st_context_iface *stsrci)
{
   struct st_context *st = (struct st_context *) stctxi;
   struct st_context *src = (struct st_context *) stsrci;

   return _mesa_share_state(st->ctx, src->ctx);
}


static void
st_context_destroy(struct st_context_iface *stctxi)
{
   struct st_context *st = (struct st_context *) stctxi;
   st_destroy_context(st);
}


static void
st_start_thread(struct st_context_iface *stctxi)
{
   struct st_context *st = (struct st_context *) stctxi;

   _mesa_glthread_init(st->ctx);
}


static void
st_thread_finish(struct st_context_iface *stctxi)
{
   struct st_context *st = (struct st_context *) stctxi;

   _mesa_glthread_finish(st->ctx);
}


static void
st_manager_destroy(struct st_manager *smapi)
{
   struct st_manager_private *smPriv = smapi->st_manager_private;

   if (smPriv && smPriv->stfbi_ht) {
      _mesa_hash_table_destroy(smPriv->stfbi_ht, NULL);
      mtx_destroy(&smPriv->st_mutex);
      free(smPriv);
      smapi->st_manager_private = NULL;
   }
}


static struct st_context_iface *
st_api_create_context(struct st_api *stapi, struct st_manager *smapi,
                      const struct st_context_attribs *attribs,
                      enum st_context_error *error,
                      struct st_context_iface *shared_stctxi)
{
   struct st_context *shared_ctx = (struct st_context *) shared_stctxi;
   struct st_context *st;
   struct pipe_context *pipe;
   struct gl_config mode;
   gl_api api;
   bool no_error = false;
   unsigned ctx_flags = PIPE_CONTEXT_PREFER_THREADED;

   if (!(stapi->profile_mask & (1 << attribs->profile)))
      return NULL;

   switch (attribs->profile) {
   case ST_PROFILE_DEFAULT:
      api = API_OPENGL_COMPAT;
      break;
   case ST_PROFILE_OPENGL_ES1:
      api = API_OPENGLES;
      break;
   case ST_PROFILE_OPENGL_ES2:
      api = API_OPENGLES2;
      break;
   case ST_PROFILE_OPENGL_CORE:
      api = API_OPENGL_CORE;
      break;
   default:
      *error = ST_CONTEXT_ERROR_BAD_API;
      return NULL;
   }

   /* Create a hash table for the framebuffer interface objects
    * if it has not been created for this st manager.
    */
   if (smapi->st_manager_private == NULL) {
      struct st_manager_private *smPriv;

      smPriv = CALLOC_STRUCT(st_manager_private);
      mtx_init(&smPriv->st_mutex, mtx_plain);
      smPriv->stfbi_ht = _mesa_hash_table_create(NULL,
                                                 st_framebuffer_iface_hash,
                                                 st_framebuffer_iface_equal);
      smapi->st_manager_private = smPriv;
      smapi->destroy = st_manager_destroy;
   }

   if (attribs->flags & ST_CONTEXT_FLAG_ROBUST_ACCESS)
      ctx_flags |= PIPE_CONTEXT_ROBUST_BUFFER_ACCESS;

   if (attribs->flags & ST_CONTEXT_FLAG_NO_ERROR)
      no_error = true;

   if (attribs->flags & ST_CONTEXT_FLAG_LOW_PRIORITY)
      ctx_flags |= PIPE_CONTEXT_LOW_PRIORITY;
   else if (attribs->flags & ST_CONTEXT_FLAG_HIGH_PRIORITY)
      ctx_flags |= PIPE_CONTEXT_HIGH_PRIORITY;

   pipe = smapi->screen->context_create(smapi->screen, NULL, ctx_flags);
   if (!pipe) {
      *error = ST_CONTEXT_ERROR_NO_MEMORY;
      return NULL;
   }

   st_visual_to_context_mode(&attribs->visual, &mode);
   st = st_create_context(api, pipe, &mode, shared_ctx,
                          &attribs->options, no_error);
   if (!st) {
      *error = ST_CONTEXT_ERROR_NO_MEMORY;
      pipe->destroy(pipe);
      return NULL;
   }

   if (attribs->flags & ST_CONTEXT_FLAG_DEBUG) {
      if (!_mesa_set_debug_state_int(st->ctx, GL_DEBUG_OUTPUT, GL_TRUE)) {
         *error = ST_CONTEXT_ERROR_NO_MEMORY;
         return NULL;
      }

      st->ctx->Const.ContextFlags |= GL_CONTEXT_FLAG_DEBUG_BIT;
   }

   if (st->ctx->Const.ContextFlags & GL_CONTEXT_FLAG_DEBUG_BIT) {
      st_update_debug_callback(st);
   }

   if (attribs->flags & ST_CONTEXT_FLAG_FORWARD_COMPATIBLE)
      st->ctx->Const.ContextFlags |= GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT;
   if (attribs->flags & ST_CONTEXT_FLAG_ROBUST_ACCESS) {
      st->ctx->Const.ContextFlags |= GL_CONTEXT_FLAG_ROBUST_ACCESS_BIT_ARB;
      st->ctx->Const.RobustAccess = GL_TRUE;
   }
   if (attribs->flags & ST_CONTEXT_FLAG_RESET_NOTIFICATION_ENABLED) {
      st->ctx->Const.ResetStrategy = GL_LOSE_CONTEXT_ON_RESET_ARB;
      st_install_device_reset_callback(st);
   }

   if (attribs->flags & ST_CONTEXT_FLAG_RELEASE_NONE)
       st->ctx->Const.ContextReleaseBehavior = GL_NONE;

   /* need to perform version check */
   if (attribs->major > 1 || attribs->minor > 0) {
      /* Is the actual version less than the requested version?
       */
      if (st->ctx->Version < attribs->major * 10U + attribs->minor) {
         *error = ST_CONTEXT_ERROR_BAD_VERSION;
         st_destroy_context(st);
         return NULL;
      }
   }

   st->invalidate_on_gl_viewport =
      smapi->get_param(smapi, ST_MANAGER_BROKEN_INVALIDATE);

   st->iface.destroy = st_context_destroy;
   st->iface.flush = st_context_flush;
   st->iface.teximage = st_context_teximage;
   st->iface.copy = st_context_copy;
   st->iface.share = st_context_share;
   st->iface.start_thread = st_start_thread;
   st->iface.thread_finish = st_thread_finish;
   st->iface.st_context_private = (void *) smapi;
   st->iface.cso_context = st->cso_context;
   st->iface.pipe = st->pipe;
   st->iface.state_manager = smapi;

   *error = ST_CONTEXT_SUCCESS;
   return &st->iface;
}


static struct st_context_iface *
st_api_get_current(struct st_api *stapi)
{
   GET_CURRENT_CONTEXT(ctx);
   struct st_context *st = ctx ? ctx->st : NULL;

   return st ? &st->iface : NULL;
}


static struct st_framebuffer *
st_framebuffer_reuse_or_create(struct st_context *st,
                               struct gl_framebuffer *fb,
                               struct st_framebuffer_iface *stfbi)
{
   struct st_framebuffer *cur = NULL, *stfb = NULL;

   if (!stfbi)
      return NULL;

   /* Check if there is already a framebuffer object for the specified
    * framebuffer interface in this context. If there is one, use it.
    */
   LIST_FOR_EACH_ENTRY(cur, &st->winsys_buffers, head) {
      if (cur->iface_ID == stfbi->ID) {
         st_framebuffer_reference(&stfb, cur);
         break;
      }
   }

   /* If there is not already a framebuffer object, create one */
   if (stfb == NULL) {
      cur = st_framebuffer_create(st, stfbi);

      if (cur) {
         /* add the referenced framebuffer interface object to
          * the framebuffer interface object hash table.
          */
         if (!st_framebuffer_iface_insert(stfbi->state_manager, stfbi)) {
            st_framebuffer_reference(&cur, NULL);
            return NULL;
         }

         /* add to the context's winsys buffers list */
         LIST_ADD(&cur->head, &st->winsys_buffers);

         st_framebuffer_reference(&stfb, cur);
      }
   }

   return stfb;
}


static boolean
st_api_make_current(struct st_api *stapi, struct st_context_iface *stctxi,
                    struct st_framebuffer_iface *stdrawi,
                    struct st_framebuffer_iface *streadi)
{
   struct st_context *st = (struct st_context *) stctxi;
   struct st_framebuffer *stdraw, *stread;
   boolean ret;

   _glapi_check_multithread();

   if (st) {
      /* reuse or create the draw fb */
      stdraw = st_framebuffer_reuse_or_create(st,
            st->ctx->WinSysDrawBuffer, stdrawi);
      if (streadi != stdrawi) {
         /* do the same for the read fb */
         stread = st_framebuffer_reuse_or_create(st,
               st->ctx->WinSysReadBuffer, streadi);
      }
      else {
         stread = NULL;
         /* reuse the draw fb for the read fb */
         if (stdraw)
            st_framebuffer_reference(&stread, stdraw);
      }

      if (stdraw && stread) {
         st_framebuffer_validate(stdraw, st);
         if (stread != stdraw)
            st_framebuffer_validate(stread, st);

         ret = _mesa_make_current(st->ctx, &stdraw->Base, &stread->Base);

         st->draw_stamp = stdraw->stamp - 1;
         st->read_stamp = stread->stamp - 1;
         st_context_validate(st, stdraw, stread);
      }
      else {
         struct gl_framebuffer *incomplete = _mesa_get_incomplete_framebuffer();
         ret = _mesa_make_current(st->ctx, incomplete, incomplete);
      }

      st_framebuffer_reference(&stdraw, NULL);
      st_framebuffer_reference(&stread, NULL);

      /* Purge the context's winsys_buffers list in case any
       * of the referenced drawables no longer exist.
       */
      st_framebuffers_purge(st);
   }
   else {
      ret = _mesa_make_current(NULL, NULL, NULL);
   }

   return ret;
}


static void
st_api_destroy(struct st_api *stapi)
{
}


/**
 * Flush the front buffer if the current context renders to the front buffer.
 */
void
st_manager_flush_frontbuffer(struct st_context *st)
{
   struct st_framebuffer *stfb = st_ws_framebuffer(st->ctx->DrawBuffer);
   struct st_renderbuffer *strb = NULL;

   if (stfb)
      strb = st_renderbuffer(stfb->Base.Attachment[BUFFER_FRONT_LEFT].
                             Renderbuffer);

   /* Do we have a front color buffer and has it been drawn to since last
    * frontbuffer flush?
    */
   if (strb && strb->defined) {
      stfb->iface->flush_front(&st->iface, stfb->iface,
                               ST_ATTACHMENT_FRONT_LEFT);
      strb->defined = GL_FALSE;

      /* Trigger an update of strb->defined on next draw */
      st->dirty |= ST_NEW_FB_STATE;
   }
}


/**
 * Re-validate the framebuffers.
 */
void
st_manager_validate_framebuffers(struct st_context *st)
{
   struct st_framebuffer *stdraw = st_ws_framebuffer(st->ctx->DrawBuffer);
   struct st_framebuffer *stread = st_ws_framebuffer(st->ctx->ReadBuffer);

   if (stdraw)
      st_framebuffer_validate(stdraw, st);
   if (stread && stread != stdraw)
      st_framebuffer_validate(stread, st);

   st_context_validate(st, stdraw, stread);
}


/**
 * Flush any outstanding swapbuffers on the current draw framebuffer.
 */
void
st_manager_flush_swapbuffers(void)
{
   GET_CURRENT_CONTEXT(ctx);
   struct st_context *st = (ctx) ? ctx->st : NULL;
   struct st_framebuffer *stfb;

   if (!st)
      return;

   stfb = st_ws_framebuffer(ctx->DrawBuffer);
   if (!stfb || !stfb->iface->flush_swapbuffers)
      return;

   stfb->iface->flush_swapbuffers(&st->iface, stfb->iface);
}


/**
 * Add a color renderbuffer on demand.  The FBO must correspond to a window,
 * not a user-created FBO.
 */
boolean
st_manager_add_color_renderbuffer(struct st_context *st,
                                  struct gl_framebuffer *fb,
                                  gl_buffer_index idx)
{
   struct st_framebuffer *stfb = st_ws_framebuffer(fb);

   /* FBO */
   if (!stfb)
      return FALSE;

   assert(_mesa_is_winsys_fbo(fb));

   if (stfb->Base.Attachment[idx].Renderbuffer)
      return TRUE;

   switch (idx) {
   case BUFFER_FRONT_LEFT:
   case BUFFER_BACK_LEFT:
   case BUFFER_FRONT_RIGHT:
   case BUFFER_BACK_RIGHT:
      break;
   default:
      return FALSE;
   }

   if (!st_framebuffer_add_renderbuffer(stfb, idx))
      return FALSE;

   st_framebuffer_update_attachments(stfb);

   /*
    * Force a call to the state tracker manager to validate the
    * new renderbuffer. It might be that there is a window system
    * renderbuffer available.
    */
   if (stfb->iface)
      stfb->iface_stamp = p_atomic_read(&stfb->iface->stamp) - 1;

   st_invalidate_buffers(st);

   return TRUE;
}


static unsigned
get_version(struct pipe_screen *screen,
            struct st_config_options *options, gl_api api)
{
   struct gl_constants consts = {0};
   struct gl_extensions extensions = {0};
   GLuint version;

   if (_mesa_override_gl_version_contextless(&consts, &api, &version)) {
      return version;
   }

   _mesa_init_constants(&consts, api);
   _mesa_init_extensions(&extensions);

   st_init_limits(screen, &consts, &extensions, api);
   st_init_extensions(screen, &consts, &extensions, options, api);

   return _mesa_get_version(&extensions, &consts, api);
}


static void
st_api_query_versions(struct st_api *stapi, struct st_manager *sm,
                      struct st_config_options *options,
                      int *gl_core_version,
                      int *gl_compat_version,
                      int *gl_es1_version,
                      int *gl_es2_version)
{
   *gl_core_version = get_version(sm->screen, options, API_OPENGL_CORE);
   *gl_compat_version = get_version(sm->screen, options, API_OPENGL_COMPAT);
   *gl_es1_version = get_version(sm->screen, options, API_OPENGLES);
   *gl_es2_version = get_version(sm->screen, options, API_OPENGLES2);
}


static const struct st_api st_gl_api = {
   .name = "Mesa " PACKAGE_VERSION,
   .api = ST_API_OPENGL,
   .profile_mask = ST_PROFILE_DEFAULT_MASK |
                   ST_PROFILE_OPENGL_CORE_MASK |
                   ST_PROFILE_OPENGL_ES1_MASK |
                   ST_PROFILE_OPENGL_ES2_MASK |
                   0,
   .feature_mask = ST_API_FEATURE_MS_VISUALS_MASK,
   .destroy = st_api_destroy,
   .query_versions = st_api_query_versions,
   .create_context = st_api_create_context,
   .make_current = st_api_make_current,
   .get_current = st_api_get_current,
   .destroy_drawable = st_api_destroy_drawable,
};


struct st_api *
st_gl_api_create(void)
{
   return (struct st_api *) &st_gl_api;
}
