/**************************************************************************
 *
 * Copyright 2010 VMware, Inc.
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


#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_debug.h"
#include "util/simple_list.h"

#include "rbug_public.h"
#include "rbug_screen.h"
#include "rbug_context.h"
#include "rbug_objects.h"

DEBUG_GET_ONCE_BOOL_OPTION(rbug, "GALLIUM_RBUG", false)

static void
rbug_screen_destroy(struct pipe_screen *_screen)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   screen->destroy(screen);

   FREE(rb_screen);
}

static const char *
rbug_screen_get_name(struct pipe_screen *_screen)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_name(screen);
}

static const char *
rbug_screen_get_vendor(struct pipe_screen *_screen)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_vendor(screen);
}

static const char *
rbug_screen_get_device_vendor(struct pipe_screen *_screen)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_device_vendor(screen);
}

static const void *
rbug_screen_get_compiler_options(struct pipe_screen *_screen,
                                 enum pipe_shader_ir ir,
                                 enum pipe_shader_type shader)
{
   struct pipe_screen *screen = rbug_screen(_screen)->screen;

   return screen->get_compiler_options(screen, ir, shader);
}

static struct disk_cache *
rbug_screen_get_disk_shader_cache(struct pipe_screen *_screen)
{
   struct pipe_screen *screen = rbug_screen(_screen)->screen;

   return screen->get_disk_shader_cache(screen);
}

static int
rbug_screen_get_param(struct pipe_screen *_screen,
                      enum pipe_cap param)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_param(screen,
                            param);
}

static int
rbug_screen_get_shader_param(struct pipe_screen *_screen,
                             enum pipe_shader_type shader,
                             enum pipe_shader_cap param)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_shader_param(screen, shader,
                            param);
}

static float
rbug_screen_get_paramf(struct pipe_screen *_screen,
                       enum pipe_capf param)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_paramf(screen,
                             param);
}

static bool
rbug_screen_is_format_supported(struct pipe_screen *_screen,
                                enum pipe_format format,
                                enum pipe_texture_target target,
                                unsigned sample_count,
                                unsigned storage_sample_count,
                                unsigned tex_usage)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->is_format_supported(screen,
                                      format,
                                      target,
                                      sample_count,
                                      storage_sample_count,
                                      tex_usage);
}

static void
rbug_screen_query_dmabuf_modifiers(struct pipe_screen *_screen,
                                   enum pipe_format format, int max,
                                   uint64_t *modifiers,
                                   unsigned int *external_only, int *count)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   screen->query_dmabuf_modifiers(screen,
                                  format,
                                  max,
                                  modifiers,
                                  external_only,
                                  count);
}

static bool
rbug_screen_is_dmabuf_modifier_supported(struct pipe_screen *_screen,
                                         uint64_t modifier,
                                         enum pipe_format format,
                                         bool *external_only)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->is_dmabuf_modifier_supported(screen,
                                               modifier,
                                               format,
                                               external_only);
}

static unsigned int
rbug_screen_get_dmabuf_modifier_planes(struct pipe_screen *_screen,
                                       uint64_t modifier,
                                       enum pipe_format format)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_dmabuf_modifier_planes(screen, modifier, format);
}

static int
rbug_screen_get_sparse_texture_virtual_page_size(struct pipe_screen *_screen,
                                                 enum pipe_texture_target target,
                                                 bool multi_sample,
                                                 enum pipe_format format,
                                                 unsigned offset, unsigned size,
                                                 int *x, int *y, int *z)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->get_sparse_texture_virtual_page_size(screen, target, multi_sample,
                                                       format, offset, size, x, y, z);
}

static struct pipe_context *
rbug_screen_context_create(struct pipe_screen *_screen,
                           void *priv, unsigned flags)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_context *result;

   result = screen->context_create(screen, priv, flags);
   if (result)
      return rbug_context_create(_screen, result);
   return NULL;
}

static bool
rbug_screen_can_create_resource(struct pipe_screen *_screen,
                                const struct pipe_resource *templat)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->can_create_resource(screen,
                                      templat);
}

static struct pipe_resource *
rbug_screen_resource_create(struct pipe_screen *_screen,
                            const struct pipe_resource *templat)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *result;

   result = screen->resource_create(screen,
                                    templat);

   if (result)
      return rbug_resource_create(rb_screen, result);
   return NULL;
}

static struct pipe_resource *
rbug_screen_resource_create_with_modifiers(struct pipe_screen *_screen,
                                           const struct pipe_resource *templat,
                                           const uint64_t *modifiers, int count)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *result;

   result = screen->resource_create_with_modifiers(screen,
                                                   templat,
                                                   modifiers,
                                                   count);

   if (result)
      return rbug_resource_create(rb_screen, result);
   return NULL;
}

static struct pipe_resource *
rbug_screen_resource_from_handle(struct pipe_screen *_screen,
                                 const struct pipe_resource *templ,
                                 struct winsys_handle *handle,
                                 unsigned usage)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *result;

   result = screen->resource_from_handle(screen, templ, handle, usage);

   result = rbug_resource_create(rbug_screen(_screen), result);

   return result;
}

static bool
rbug_screen_check_resource_capability(struct pipe_screen *_screen,
                                      struct pipe_resource *_resource,
                                      unsigned bind)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct rbug_resource *rb_resource = rbug_resource(_resource);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *resource = rb_resource->resource;

   return screen->check_resource_capability(screen, resource, bind);
}

static bool
rbug_screen_resource_get_handle(struct pipe_screen *_screen,
                                struct pipe_context *_pipe,
                                struct pipe_resource *_resource,
                                struct winsys_handle *handle,
                                unsigned usage)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct rbug_context *rb_pipe = rbug_context(_pipe);
   struct rbug_resource *rb_resource = rbug_resource(_resource);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *resource = rb_resource->resource;

   return screen->resource_get_handle(screen, rb_pipe ? rb_pipe->pipe : NULL,
                                      resource, handle, usage);
}

static bool
rbug_screen_resource_get_param(struct pipe_screen *_screen,
                               struct pipe_context *_pipe,
                               struct pipe_resource *_resource,
                               unsigned plane,
                               unsigned layer,
                               unsigned level,
                               enum pipe_resource_param param,
                               unsigned handle_usage,
                               uint64_t *value)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct rbug_context *rb_pipe = rbug_context(_pipe);
   struct rbug_resource *rb_resource = rbug_resource(_resource);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *resource = rb_resource->resource;

   return screen->resource_get_param(screen, rb_pipe ? rb_pipe->pipe : NULL,
                                     resource, plane, layer, level, param,
                                     handle_usage, value);
}


static void
rbug_screen_resource_get_info(struct pipe_screen *_screen,
                              struct pipe_resource *_resource,
                              unsigned *stride,
                              unsigned *offset)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct rbug_resource *rb_resource = rbug_resource(_resource);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *resource = rb_resource->resource;

   screen->resource_get_info(screen, resource, stride, offset);
}

static void
rbug_screen_resource_changed(struct pipe_screen *_screen,
                             struct pipe_resource *_resource)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct rbug_resource *rb_resource = rbug_resource(_resource);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *resource = rb_resource->resource;

   screen->resource_changed(screen, resource);
}

static void
rbug_screen_resource_destroy(struct pipe_screen *screen,
                             struct pipe_resource *_resource)
{
   rbug_resource_destroy(rbug_resource(_resource));
}

static void
rbug_screen_flush_frontbuffer(struct pipe_screen *_screen,
                              struct pipe_context *_ctx,
                              struct pipe_resource *_resource,
                              unsigned level, unsigned layer,
                              void *context_private, struct pipe_box *sub_box)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct rbug_resource *rb_resource = rbug_resource(_resource);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_resource *resource = rb_resource->resource;
   struct pipe_context *ctx = _ctx ? rbug_context(_ctx)->pipe : NULL;

   screen->flush_frontbuffer(screen,
                             ctx,
                             resource,
                             level, layer,
                             context_private, sub_box);
}

static void
rbug_screen_fence_reference(struct pipe_screen *_screen,
                            struct pipe_fence_handle **ptr,
                            struct pipe_fence_handle *fence)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   screen->fence_reference(screen,
                           ptr,
                           fence);
}

static bool
rbug_screen_fence_finish(struct pipe_screen *_screen,
                         struct pipe_context *_ctx,
                         struct pipe_fence_handle *fence,
                         uint64_t timeout)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;
   struct pipe_context *ctx = _ctx ? rbug_context(_ctx)->pipe : NULL;

   return screen->fence_finish(screen, ctx, fence, timeout);
}

static int
rbug_screen_fence_get_fd(struct pipe_screen *_screen,
                         struct pipe_fence_handle *fence)
{
   struct rbug_screen *rb_screen = rbug_screen(_screen);
   struct pipe_screen *screen = rb_screen->screen;

   return screen->fence_get_fd(screen, fence);
}

static char *
rbug_screen_finalize_nir(struct pipe_screen *_screen, void *nir)
{
   struct pipe_screen *screen = rbug_screen(_screen)->screen;

   return screen->finalize_nir(screen, nir);
}

bool
rbug_enabled()
{
   return debug_get_option_rbug();
}

struct pipe_screen *
rbug_screen_create(struct pipe_screen *screen)
{
   struct rbug_screen *rb_screen;

   if (!debug_get_option_rbug())
      return screen;

   rb_screen = CALLOC_STRUCT(rbug_screen);
   if (!rb_screen)
      return screen;

   (void) mtx_init(&rb_screen->list_mutex, mtx_plain);
   make_empty_list(&rb_screen->contexts);
   make_empty_list(&rb_screen->resources);
   make_empty_list(&rb_screen->surfaces);
   make_empty_list(&rb_screen->transfers);

#define SCR_INIT(_member) \
   rb_screen->base._member = screen->_member ? rbug_screen_##_member : NULL

   rb_screen->base.destroy = rbug_screen_destroy;
   rb_screen->base.get_name = rbug_screen_get_name;
   rb_screen->base.get_vendor = rbug_screen_get_vendor;
   SCR_INIT(get_compiler_options);
   SCR_INIT(get_disk_shader_cache);
   rb_screen->base.get_device_vendor = rbug_screen_get_device_vendor;
   rb_screen->base.get_param = rbug_screen_get_param;
   rb_screen->base.get_shader_param = rbug_screen_get_shader_param;
   rb_screen->base.get_paramf = rbug_screen_get_paramf;
   rb_screen->base.is_format_supported = rbug_screen_is_format_supported;
   SCR_INIT(query_dmabuf_modifiers);
   SCR_INIT(is_dmabuf_modifier_supported);
   SCR_INIT(get_dmabuf_modifier_planes);
   rb_screen->base.context_create = rbug_screen_context_create;
   SCR_INIT(can_create_resource);
   rb_screen->base.resource_create = rbug_screen_resource_create;
   SCR_INIT(resource_create_with_modifiers);
   rb_screen->base.resource_from_handle = rbug_screen_resource_from_handle;
   SCR_INIT(check_resource_capability);
   rb_screen->base.resource_get_handle = rbug_screen_resource_get_handle;
   SCR_INIT(resource_get_param);
   SCR_INIT(resource_get_info);
   SCR_INIT(resource_changed);
   rb_screen->base.resource_destroy = rbug_screen_resource_destroy;
   rb_screen->base.flush_frontbuffer = rbug_screen_flush_frontbuffer;
   rb_screen->base.fence_reference = rbug_screen_fence_reference;
   rb_screen->base.fence_finish = rbug_screen_fence_finish;
   rb_screen->base.fence_get_fd = rbug_screen_fence_get_fd;
   SCR_INIT(finalize_nir);
   SCR_INIT(get_sparse_texture_virtual_page_size);

   rb_screen->screen = screen;

   rb_screen->private_context = screen->context_create(screen, NULL, 0);
   if (!rb_screen->private_context)
      goto err_free;

   rb_screen->rbug = rbug_start(rb_screen);

   if (!rb_screen->rbug)
      goto err_context;

   return &rb_screen->base;

err_context:
   rb_screen->private_context->destroy(rb_screen->private_context);
err_free:
   FREE(rb_screen);
   return screen;
}
