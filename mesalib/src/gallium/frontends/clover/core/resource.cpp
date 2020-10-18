//
// Copyright 2012 Francisco Jerez
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

#include "core/resource.hpp"
#include "core/memory.hpp"
#include "pipe/p_screen.h"
#include "util/u_sampler.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"

using namespace clover;

namespace {
   class box {
   public:
      box(const resource::vector &origin, const resource::vector &size) :
        pipe({ (int)origin[0], (int16_t)origin[1],
               (int16_t)origin[2], (int)size[0],
               (int16_t)size[1], (int16_t)size[2] }) {
      }

      operator const pipe_box *() {
         return &pipe;
      }

   protected:
      pipe_box pipe;
   };
}

resource::resource(clover::device &dev, memory_obj &obj) :
   device(dev), obj(obj), pipe(NULL), offset() {
}

resource::~resource() {
}

void
resource::copy(command_queue &q, const vector &origin, const vector &region,
               resource &src_res, const vector &src_origin) {
   auto p = offset + origin;

   q.pipe->resource_copy_region(q.pipe, pipe, 0, p[0], p[1], p[2],
                                src_res.pipe, 0,
                                box(src_res.offset + src_origin, region));
}

void *
resource::add_map(command_queue &q, cl_map_flags flags, bool blocking,
                  const vector &origin, const vector &region) {
   maps.emplace_back(q, *this, flags, blocking, origin, region);
   return maps.back();
}

void
resource::del_map(void *p) {
   erase_if([&](const mapping &m) {
         return static_cast<void *>(m) == p;
      }, maps);
}

unsigned
resource::map_count() const {
   return maps.size();
}

pipe_sampler_view *
resource::bind_sampler_view(command_queue &q) {
   pipe_sampler_view info;

   u_sampler_view_default_template(&info, pipe, pipe->format);
   return q.pipe->create_sampler_view(q.pipe, pipe, &info);
}

void
resource::unbind_sampler_view(command_queue &q,
                              pipe_sampler_view *st) {
   q.pipe->sampler_view_destroy(q.pipe, st);
}

pipe_surface *
resource::bind_surface(command_queue &q, bool rw) {
   pipe_surface info {};

   info.format = pipe->format;
   info.writable = rw;

   if (pipe->target == PIPE_BUFFER)
      info.u.buf.last_element = pipe->width0 - 1;

   return q.pipe->create_surface(q.pipe, pipe, &info);
}

void
resource::unbind_surface(command_queue &q, pipe_surface *st) {
   q.pipe->surface_destroy(q.pipe, st);
}

root_resource::root_resource(clover::device &dev, memory_obj &obj,
                             command_queue &q, const std::string &data) :
   resource(dev, obj) {
   pipe_resource info {};
   const bool user_ptr_support = dev.pipe->get_param(dev.pipe,
         PIPE_CAP_RESOURCE_FROM_USER_MEMORY);

   if (image *img = dynamic_cast<image *>(&obj)) {
      info.format = translate_format(img->format());
      info.width0 = img->width();
      info.height0 = img->height();
      info.depth0 = img->depth();
   } else {
      info.width0 = obj.size();
      info.height0 = 1;
      info.depth0 = 1;
   }

   info.array_size = 1;
   info.target = translate_target(obj.type());
   info.bind = (PIPE_BIND_SAMPLER_VIEW |
                PIPE_BIND_COMPUTE_RESOURCE |
                PIPE_BIND_GLOBAL);

   if (obj.flags() & CL_MEM_USE_HOST_PTR && user_ptr_support) {
      // Page alignment is normally required for this, just try, hope for the
      // best and fall back if it fails.
      pipe = dev.pipe->resource_from_user_memory(dev.pipe, &info, obj.host_ptr());
      if (pipe)
         return;
   }

   if (obj.flags() & (CL_MEM_ALLOC_HOST_PTR | CL_MEM_USE_HOST_PTR)) {
      info.usage = PIPE_USAGE_STAGING;
   }

   pipe = dev.pipe->resource_create(dev.pipe, &info);
   if (!pipe)
      throw error(CL_OUT_OF_RESOURCES);

   if (obj.flags() & (CL_MEM_USE_HOST_PTR | CL_MEM_COPY_HOST_PTR)) {
      const void *data_ptr = !data.empty() ? data.data() : obj.host_ptr();
      box rect { {{ 0, 0, 0 }}, {{ info.width0, info.height0, info.depth0 }} };
      unsigned cpp = util_format_get_blocksize(info.format);

      if (pipe->target == PIPE_BUFFER)
         q.pipe->buffer_subdata(q.pipe, pipe, PIPE_TRANSFER_WRITE,
                                0, info.width0, data_ptr);
      else
         q.pipe->texture_subdata(q.pipe, pipe, 0, PIPE_TRANSFER_WRITE,
                                 rect, data_ptr, cpp * info.width0,
                                 cpp * info.width0 * info.height0);
   }
}

root_resource::root_resource(clover::device &dev, memory_obj &obj,
                             root_resource &r) :
   resource(dev, obj) {
   assert(0); // XXX -- resource shared among dev and r.dev
}

root_resource::~root_resource() {
   pipe_resource_reference(&this->pipe, NULL);
}

sub_resource::sub_resource(resource &r, const vector &offset) :
   resource(r.device(), r.obj) {
   this->pipe = r.pipe;
   this->offset = r.offset + offset;
}

mapping::mapping(command_queue &q, resource &r,
                 cl_map_flags flags, bool blocking,
                 const resource::vector &origin,
                 const resource::vector &region) :
   pctx(q.pipe), pres(NULL) {
   unsigned usage = ((flags & CL_MAP_WRITE ? PIPE_TRANSFER_WRITE : 0 ) |
                     (flags & CL_MAP_READ ? PIPE_TRANSFER_READ : 0 ) |
                     (flags & CL_MAP_WRITE_INVALIDATE_REGION ?
                      PIPE_TRANSFER_DISCARD_RANGE : 0) |
                     (!blocking ? PIPE_TRANSFER_UNSYNCHRONIZED : 0));

   p = pctx->transfer_map(pctx, r.pipe, 0, usage,
                          box(origin + r.offset, region), &pxfer);
   if (!p) {
      pxfer = NULL;
      throw error(CL_OUT_OF_RESOURCES);
   }
   pipe_resource_reference(&pres, r.pipe);
}

mapping::mapping(mapping &&m) :
   pctx(m.pctx), pxfer(m.pxfer), pres(m.pres), p(m.p) {
   m.pctx = NULL;
   m.pxfer = NULL;
   m.pres = NULL;
   m.p = NULL;
}

mapping::~mapping() {
   if (pxfer) {
      pctx->transfer_unmap(pctx, pxfer);
   }
   pipe_resource_reference(&pres, NULL);
}

mapping &
mapping::operator=(mapping m) {
   std::swap(pctx, m.pctx);
   std::swap(pxfer, m.pxfer);
   std::swap(pres, m.pres);
   std::swap(p, m.p);
   return *this;
}
