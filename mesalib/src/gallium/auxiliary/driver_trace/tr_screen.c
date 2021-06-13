/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
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

#include "util/format/u_format.h"
#include "util/u_memory.h"
#include "util/simple_list.h"

#include "tr_dump.h"
#include "tr_dump_defines.h"
#include "tr_dump_state.h"
#include "tr_texture.h"
#include "tr_context.h"
#include "tr_screen.h"
#include "tr_public.h"


static bool trace = false;

static const char *
trace_screen_get_name(struct pipe_screen *_screen)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   const char *result;

   trace_dump_call_begin("pipe_screen", "get_name");

   trace_dump_arg(ptr, screen);

   result = screen->get_name(screen);

   trace_dump_ret(string, result);

   trace_dump_call_end();

   return result;
}


static const char *
trace_screen_get_vendor(struct pipe_screen *_screen)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   const char *result;

   trace_dump_call_begin("pipe_screen", "get_vendor");

   trace_dump_arg(ptr, screen);

   result = screen->get_vendor(screen);

   trace_dump_ret(string, result);

   trace_dump_call_end();

   return result;
}


static const char *
trace_screen_get_device_vendor(struct pipe_screen *_screen)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   const char *result;

   trace_dump_call_begin("pipe_screen", "get_device_vendor");

   trace_dump_arg(ptr, screen);

   result = screen->get_device_vendor(screen);

   trace_dump_ret(string, result);

   trace_dump_call_end();

   return result;
}


static const void *
trace_screen_get_compiler_options(struct pipe_screen *_screen,
                                  enum pipe_shader_ir ir,
                                  enum pipe_shader_type shader)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;

   return screen->get_compiler_options(screen, ir, shader);
}


static struct disk_cache *
trace_screen_get_disk_shader_cache(struct pipe_screen *_screen)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;

   trace_dump_call_begin("pipe_screen", "get_disk_shader_cache");

   trace_dump_arg(ptr, screen);

   struct disk_cache *result = screen->get_disk_shader_cache(screen);

   trace_dump_ret(ptr, result);

   trace_dump_call_end();

   return result;
}


static int
trace_screen_get_param(struct pipe_screen *_screen,
                       enum pipe_cap param)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   int result;

   trace_dump_call_begin("pipe_screen", "get_param");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(int, param);

   result = screen->get_param(screen, param);

   trace_dump_ret(int, result);

   trace_dump_call_end();

   return result;
}


static int
trace_screen_get_shader_param(struct pipe_screen *_screen,
                              enum pipe_shader_type shader,
                              enum pipe_shader_cap param)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   int result;

   trace_dump_call_begin("pipe_screen", "get_shader_param");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(uint, shader);
   trace_dump_arg(int, param);

   result = screen->get_shader_param(screen, shader, param);

   trace_dump_ret(int, result);

   trace_dump_call_end();

   return result;
}


static float
trace_screen_get_paramf(struct pipe_screen *_screen,
                        enum pipe_capf param)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   float result;

   trace_dump_call_begin("pipe_screen", "get_paramf");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(int, param);

   result = screen->get_paramf(screen, param);

   trace_dump_ret(float, result);

   trace_dump_call_end();

   return result;
}


static int
trace_screen_get_compute_param(struct pipe_screen *_screen,
                               enum pipe_shader_ir ir_type,
                               enum pipe_compute_cap param, void *data)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   int result;

   trace_dump_call_begin("pipe_screen", "get_compute_param");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(int, ir_type);
   trace_dump_arg(int, param);
   trace_dump_arg(ptr, data);

   result = screen->get_compute_param(screen, ir_type, param, data);

   trace_dump_ret(int, result);

   trace_dump_call_end();

   return result;
}


static bool
trace_screen_is_format_supported(struct pipe_screen *_screen,
                                 enum pipe_format format,
                                 enum pipe_texture_target target,
                                 unsigned sample_count,
                                 unsigned storage_sample_count,
                                 unsigned tex_usage)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   bool result;

   trace_dump_call_begin("pipe_screen", "is_format_supported");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(format, format);
   trace_dump_arg(int, target);
   trace_dump_arg(uint, sample_count);
   trace_dump_arg(uint, tex_usage);

   result = screen->is_format_supported(screen, format, target, sample_count,
                                        storage_sample_count, tex_usage);

   trace_dump_ret(bool, result);

   trace_dump_call_end();

   return result;
}


static struct pipe_context *
trace_screen_context_create(struct pipe_screen *_screen, void *priv,
                            unsigned flags)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   struct pipe_context *result;

   trace_dump_call_begin("pipe_screen", "context_create");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, priv);
   trace_dump_arg(uint, flags);

   result = screen->context_create(screen, priv, flags);

   trace_dump_ret(ptr, result);

   trace_dump_call_end();

   result = trace_context_create(tr_scr, result);

   return result;
}


static void
trace_screen_flush_frontbuffer(struct pipe_screen *_screen,
                               struct pipe_context *_pipe,
                               struct pipe_resource *resource,
                               unsigned level, unsigned layer,
                               void *context_private,
                               struct pipe_box *sub_box)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   struct pipe_context *pipe = _pipe ? trace_context(_pipe)->pipe : NULL;

   trace_dump_call_begin("pipe_screen", "flush_frontbuffer");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, resource);
   trace_dump_arg(uint, level);
   trace_dump_arg(uint, layer);
   /* XXX: hide, as there is nothing we can do with this
   trace_dump_arg(ptr, context_private);
   */

   screen->flush_frontbuffer(screen, pipe, resource, level, layer, context_private, sub_box);

   trace_dump_call_end();
}


static void
trace_screen_get_driver_uuid(struct pipe_screen *_screen, char *uuid)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   trace_dump_call_begin("pipe_screen", "get_driver_uuid");
   trace_dump_arg(ptr, screen);

   screen->get_driver_uuid(screen, uuid);

   trace_dump_ret(string, uuid);
   trace_dump_call_end();
}

static void
trace_screen_get_device_uuid(struct pipe_screen *_screen, char *uuid)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   trace_dump_call_begin("pipe_screen", "get_device_uuid");
   trace_dump_arg(ptr, screen);

   screen->get_device_uuid(screen, uuid);

   trace_dump_ret(string, uuid);
   trace_dump_call_end();
}


/********************************************************************
 * texture
 */


static struct pipe_resource *
trace_screen_resource_create(struct pipe_screen *_screen,
                            const struct pipe_resource *templat)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   struct pipe_resource *result;

   trace_dump_call_begin("pipe_screen", "resource_create");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(resource_template, templat);

   result = screen->resource_create(screen, templat);

   trace_dump_ret(ptr, result);

   trace_dump_call_end();

   if (result)
      result->screen = _screen;
   return result;
}

static struct pipe_resource *
trace_screen_resource_from_handle(struct pipe_screen *_screen,
                                 const struct pipe_resource *templ,
                                 struct winsys_handle *handle,
                                  unsigned usage)
{
   struct trace_screen *tr_screen = trace_screen(_screen);
   struct pipe_screen *screen = tr_screen->screen;
   struct pipe_resource *result;

   /* TODO trace call */

   result = screen->resource_from_handle(screen, templ, handle, usage);

   if (result)
      result->screen = _screen;
   return result;
}

static bool
trace_screen_check_resource_capability(struct pipe_screen *_screen,
                                       struct pipe_resource *resource,
                                       unsigned bind)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   return screen->check_resource_capability(screen, resource, bind);
}

static bool
trace_screen_resource_get_handle(struct pipe_screen *_screen,
                                 struct pipe_context *_pipe,
                                struct pipe_resource *resource,
                                struct winsys_handle *handle,
                                 unsigned usage)
{
   struct trace_screen *tr_screen = trace_screen(_screen);
   struct trace_context *tr_pipe = _pipe ? trace_context(_pipe) : NULL;
   struct pipe_screen *screen = tr_screen->screen;

   /* TODO trace call */

   return screen->resource_get_handle(screen, tr_pipe ? tr_pipe->pipe : NULL,
                                      resource, handle, usage);
}

static bool
trace_screen_resource_get_param(struct pipe_screen *_screen,
                                struct pipe_context *_pipe,
                                struct pipe_resource *resource,
                                unsigned plane,
                                unsigned layer,
                                unsigned level,
                                enum pipe_resource_param param,
                                unsigned handle_usage,
                                uint64_t *value)
{
   struct trace_screen *tr_screen = trace_screen(_screen);
   struct trace_context *tr_pipe = _pipe ? trace_context(_pipe) : NULL;
   struct pipe_screen *screen = tr_screen->screen;

   /* TODO trace call */

   return screen->resource_get_param(screen, tr_pipe ? tr_pipe->pipe : NULL,
                                     resource, plane, layer, level, param,
                                     handle_usage, value);
}

static void
trace_screen_resource_get_info(struct pipe_screen *_screen,
                               struct pipe_resource *resource,
                               unsigned *stride,
                               unsigned *offset)
{
   struct trace_screen *tr_screen = trace_screen(_screen);
   struct pipe_screen *screen = tr_screen->screen;

   /* TODO trace call */

   screen->resource_get_info(screen, resource, stride, offset);
}

static struct pipe_resource *
trace_screen_resource_from_memobj(struct pipe_screen *_screen,
                                  const struct pipe_resource *templ,
                                  struct pipe_memory_object *memobj,
                                  uint64_t offset)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   trace_dump_call_begin("pipe_screen", "resource_from_memobj");
   trace_dump_arg(ptr, screen);
   trace_dump_arg(resource_template, templ);
   trace_dump_arg(ptr, memobj);
   trace_dump_arg(uint, offset);

   struct pipe_resource *res =
      screen->resource_from_memobj(screen, templ, memobj, offset);

   if (!res)
      return NULL;
   res->screen = _screen;

   trace_dump_ret(ptr, res);
   trace_dump_call_end();
   return res;
}

static void
trace_screen_resource_changed(struct pipe_screen *_screen,
                              struct pipe_resource *resource)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;

   trace_dump_call_begin("pipe_screen", "resource_changed");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, resource);

   if (screen->resource_changed)
      screen->resource_changed(screen, resource);

   trace_dump_call_end();
}

static void
trace_screen_resource_destroy(struct pipe_screen *_screen,
			      struct pipe_resource *resource)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;

   /* Don't trace this, because due to the lack of pipe_resource wrapping,
    * we can get this call from inside of driver calls, which would try
    * to lock an already-locked mutex.
    */
   screen->resource_destroy(screen, resource);
}


/********************************************************************
 * fence
 */


static void
trace_screen_fence_reference(struct pipe_screen *_screen,
                             struct pipe_fence_handle **pdst,
                             struct pipe_fence_handle *src)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   struct pipe_fence_handle *dst;

   assert(pdst);
   dst = *pdst;
   
   trace_dump_call_begin("pipe_screen", "fence_reference");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, dst);
   trace_dump_arg(ptr, src);

   screen->fence_reference(screen, pdst, src);

   trace_dump_call_end();
}


static int
trace_screen_fence_get_fd(struct pipe_screen *_screen,
                          struct pipe_fence_handle *fence)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   int result;

   trace_dump_call_begin("pipe_screen", "fence_get_fd");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, fence);

   result = screen->fence_get_fd(screen, fence);

   trace_dump_ret(int, result);

   trace_dump_call_end();

   return result;
}


static bool
trace_screen_fence_finish(struct pipe_screen *_screen,
                          struct pipe_context *_ctx,
                          struct pipe_fence_handle *fence,
                          uint64_t timeout)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   struct pipe_context *ctx = _ctx ? trace_context(_ctx)->pipe : NULL;
   int result;

   trace_dump_call_begin("pipe_screen", "fence_finish");

   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, ctx);
   trace_dump_arg(ptr, fence);
   trace_dump_arg(uint, timeout);

   result = screen->fence_finish(screen, ctx, fence, timeout);

   trace_dump_ret(bool, result);

   trace_dump_call_end();

   return result;
}


/********************************************************************
 * memobj
 */

static struct pipe_memory_object *
trace_screen_memobj_create_from_handle(struct pipe_screen *_screen,
                                       struct winsys_handle *handle,
                                       bool dedicated)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   trace_dump_call_begin("pipe_screen", "memobj_create_from_handle");
   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, handle);
   trace_dump_arg(bool, dedicated);

   struct pipe_memory_object *res =
      screen->memobj_create_from_handle(screen, handle, dedicated);

   trace_dump_ret(ptr, res);
   trace_dump_call_end();

   return res;
}

static void
trace_screen_memobj_destroy(struct pipe_screen *_screen,
                            struct pipe_memory_object *memobj)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   trace_dump_call_begin("pipe_screen", "memobj_destroy");
   trace_dump_arg(ptr, screen);
   trace_dump_arg(ptr, memobj);
   trace_dump_call_end();

   screen->memobj_destroy(screen, memobj);
}


/********************************************************************
 * screen
 */

static uint64_t
trace_screen_get_timestamp(struct pipe_screen *_screen)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;
   uint64_t result;

   trace_dump_call_begin("pipe_screen", "get_timestamp");
   trace_dump_arg(ptr, screen);

   result = screen->get_timestamp(screen);

   trace_dump_ret(uint, result);
   trace_dump_call_end();

   return result;
}

static void
trace_screen_finalize_nir(struct pipe_screen *_screen, void *nir, bool optimize)
{
   struct pipe_screen *screen = trace_screen(_screen)->screen;

   screen->finalize_nir(screen, nir, optimize);
}

static void
trace_screen_destroy(struct pipe_screen *_screen)
{
   struct trace_screen *tr_scr = trace_screen(_screen);
   struct pipe_screen *screen = tr_scr->screen;

   trace_dump_call_begin("pipe_screen", "destroy");
   trace_dump_arg(ptr, screen);
   trace_dump_call_end();

   screen->destroy(screen);

   FREE(tr_scr);
}

bool
trace_enabled(void)
{
   static bool firstrun = true;

   if (!firstrun)
      return trace;
   firstrun = false;

   if(trace_dump_trace_begin()) {
      trace_dumping_start();
      trace = true;
   }

   return trace;
}

struct pipe_screen *
trace_screen_create(struct pipe_screen *screen)
{
   struct trace_screen *tr_scr;

   if (!trace_enabled())
      goto error1;

   trace_dump_call_begin("", "pipe_screen_create");

   tr_scr = CALLOC_STRUCT(trace_screen);
   if (!tr_scr)
      goto error2;

#define SCR_INIT(_member) \
   tr_scr->base._member = screen->_member ? trace_screen_##_member : NULL

   tr_scr->base.destroy = trace_screen_destroy;
   tr_scr->base.get_name = trace_screen_get_name;
   tr_scr->base.get_vendor = trace_screen_get_vendor;
   tr_scr->base.get_device_vendor = trace_screen_get_device_vendor;
   SCR_INIT(get_compiler_options);
   SCR_INIT(get_disk_shader_cache);
   tr_scr->base.get_param = trace_screen_get_param;
   tr_scr->base.get_shader_param = trace_screen_get_shader_param;
   tr_scr->base.get_paramf = trace_screen_get_paramf;
   tr_scr->base.get_compute_param = trace_screen_get_compute_param;
   tr_scr->base.is_format_supported = trace_screen_is_format_supported;
   assert(screen->context_create);
   tr_scr->base.context_create = trace_screen_context_create;
   tr_scr->base.resource_create = trace_screen_resource_create;
   tr_scr->base.resource_from_handle = trace_screen_resource_from_handle;
   SCR_INIT(check_resource_capability);
   tr_scr->base.resource_get_handle = trace_screen_resource_get_handle;
   SCR_INIT(resource_get_param);
   SCR_INIT(resource_get_info);
   SCR_INIT(resource_from_memobj);
   SCR_INIT(resource_changed);
   tr_scr->base.resource_destroy = trace_screen_resource_destroy;
   tr_scr->base.fence_reference = trace_screen_fence_reference;
   SCR_INIT(fence_get_fd);
   tr_scr->base.fence_finish = trace_screen_fence_finish;
   SCR_INIT(memobj_create_from_handle);
   SCR_INIT(memobj_destroy);
   tr_scr->base.flush_frontbuffer = trace_screen_flush_frontbuffer;
   tr_scr->base.get_timestamp = trace_screen_get_timestamp;
   SCR_INIT(get_driver_uuid);
   SCR_INIT(get_device_uuid);
   SCR_INIT(finalize_nir);

   tr_scr->screen = screen;

   trace_dump_ret(ptr, screen);
   trace_dump_call_end();

   return &tr_scr->base;

error2:
   trace_dump_ret(ptr, screen);
   trace_dump_call_end();
error1:
   return screen;
}


struct trace_screen *
trace_screen(struct pipe_screen *screen)
{
   assert(screen);
   assert(screen->destroy == trace_screen_destroy);
   return (struct trace_screen *)screen;
}
