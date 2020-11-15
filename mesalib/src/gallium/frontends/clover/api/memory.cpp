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

#include "util/format/u_format.h"
#include "util/u_math.h"
#include "api/util.hpp"
#include "core/memory.hpp"
#include "core/format.hpp"

using namespace clover;

namespace {
   cl_mem_flags
   validate_flags(cl_mem d_parent, cl_mem_flags d_flags, bool svm) {
      const cl_mem_flags dev_access_flags =
         CL_MEM_READ_WRITE | CL_MEM_WRITE_ONLY | CL_MEM_READ_ONLY;
      const cl_mem_flags host_ptr_flags =
         CL_MEM_USE_HOST_PTR | CL_MEM_ALLOC_HOST_PTR | CL_MEM_COPY_HOST_PTR;
      const cl_mem_flags host_access_flags =
         CL_MEM_HOST_WRITE_ONLY | CL_MEM_HOST_READ_ONLY | CL_MEM_HOST_NO_ACCESS;
      const cl_mem_flags svm_flags =
         CL_MEM_SVM_FINE_GRAIN_BUFFER | CL_MEM_SVM_ATOMICS;

      const cl_mem_flags valid_flags =
         dev_access_flags
            | (svm || d_parent ? 0 : host_ptr_flags)
            | (svm ? svm_flags : host_access_flags);

      if ((d_flags & ~valid_flags) ||
          util_bitcount(d_flags & dev_access_flags) > 1 ||
          util_bitcount(d_flags & host_access_flags) > 1)
         throw error(CL_INVALID_VALUE);

      if ((d_flags & CL_MEM_USE_HOST_PTR) &&
          (d_flags & (CL_MEM_COPY_HOST_PTR | CL_MEM_ALLOC_HOST_PTR)))
         throw error(CL_INVALID_VALUE);

      if ((d_flags & CL_MEM_SVM_ATOMICS) &&
          !(d_flags & CL_MEM_SVM_FINE_GRAIN_BUFFER))
         throw error(CL_INVALID_VALUE);

      if (d_parent) {
         const auto &parent = obj(d_parent);
         const cl_mem_flags flags = (d_flags |
                                     (d_flags & dev_access_flags ? 0 :
                                      parent.flags() & dev_access_flags) |
                                     (d_flags & host_access_flags ? 0 :
                                      parent.flags() & host_access_flags) |
                                     (parent.flags() & host_ptr_flags));

         if (~flags & parent.flags() & (dev_access_flags & ~CL_MEM_READ_WRITE))
            throw error(CL_INVALID_VALUE);

         // Check if new host access flags cause a mismatch between
         // host-read/write-only.
         if (!(flags & CL_MEM_HOST_NO_ACCESS) &&
             (~flags & parent.flags() & host_access_flags))
            throw error(CL_INVALID_VALUE);

         return flags;

      } else {
         return d_flags | (d_flags & dev_access_flags ? 0 : CL_MEM_READ_WRITE);
      }
   }
}

CLOVER_API cl_mem
clCreateBuffer(cl_context d_ctx, cl_mem_flags d_flags, size_t size,
               void *host_ptr, cl_int *r_errcode) try {
   const cl_mem_flags flags = validate_flags(NULL, d_flags, false);
   auto &ctx = obj(d_ctx);

   if (bool(host_ptr) != bool(flags & (CL_MEM_USE_HOST_PTR |
                                       CL_MEM_COPY_HOST_PTR)))
      throw error(CL_INVALID_HOST_PTR);

   if (!size ||
       size > fold(maximum(), cl_ulong(0),
                   map(std::mem_fn(&device::max_mem_alloc_size), ctx.devices())
          ))
      throw error(CL_INVALID_BUFFER_SIZE);

   ret_error(r_errcode, CL_SUCCESS);
   return new root_buffer(ctx, flags, size, host_ptr);

} catch (error &e) {
   ret_error(r_errcode, e);
   return NULL;
}

CLOVER_API cl_mem
clCreateSubBuffer(cl_mem d_mem, cl_mem_flags d_flags,
                  cl_buffer_create_type op,
                  const void *op_info, cl_int *r_errcode) try {
   auto &parent = obj<root_buffer>(d_mem);
   const cl_mem_flags flags = validate_flags(d_mem, d_flags, false);

   if (op == CL_BUFFER_CREATE_TYPE_REGION) {
      auto reg = reinterpret_cast<const cl_buffer_region *>(op_info);

      if (!reg ||
          reg->origin > parent.size() ||
          reg->origin + reg->size > parent.size())
         throw error(CL_INVALID_VALUE);

      if (!reg->size)
         throw error(CL_INVALID_BUFFER_SIZE);

      ret_error(r_errcode, CL_SUCCESS);
      return new sub_buffer(parent, flags, reg->origin, reg->size);

   } else {
      throw error(CL_INVALID_VALUE);
   }

} catch (error &e) {
   ret_error(r_errcode, e);
   return NULL;
}

CLOVER_API cl_mem
clCreateImage(cl_context d_ctx, cl_mem_flags d_flags,
              const cl_image_format *format,
              const cl_image_desc *desc,
              void *host_ptr, cl_int *r_errcode) try {
   auto &ctx = obj(d_ctx);

   if (!any_of(std::mem_fn(&device::image_support), ctx.devices()))
      throw error(CL_INVALID_OPERATION);

   if (!format)
      throw error(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR);

   if (!desc)
      throw error(CL_INVALID_IMAGE_DESCRIPTOR);

   if (desc->image_array_size == 0 &&
       (desc->image_type == CL_MEM_OBJECT_IMAGE1D_ARRAY ||
        desc->image_type == CL_MEM_OBJECT_IMAGE2D_ARRAY))
      throw error(CL_INVALID_IMAGE_DESCRIPTOR);

   if (!host_ptr &&
       (desc->image_row_pitch || desc->image_slice_pitch))
      throw error(CL_INVALID_IMAGE_DESCRIPTOR);

   if (desc->num_mip_levels || desc->num_samples)
      throw error(CL_INVALID_IMAGE_DESCRIPTOR);

   if (bool(desc->buffer) != (desc->image_type == CL_MEM_OBJECT_IMAGE1D_BUFFER))
      throw error(CL_INVALID_IMAGE_DESCRIPTOR);

   if (bool(host_ptr) != bool(d_flags & (CL_MEM_USE_HOST_PTR |
                                         CL_MEM_COPY_HOST_PTR)))
      throw error(CL_INVALID_HOST_PTR);

   const cl_mem_flags flags = validate_flags(desc->buffer, d_flags, false);

   if (!supported_formats(ctx, desc->image_type).count(*format))
      throw error(CL_IMAGE_FORMAT_NOT_SUPPORTED);

   ret_error(r_errcode, CL_SUCCESS);

   const size_t row_pitch = desc->image_row_pitch ? desc->image_row_pitch :
      util_format_get_blocksize(translate_format(*format)) * desc->image_width;

   switch (desc->image_type) {
   case CL_MEM_OBJECT_IMAGE2D:
      if (!desc->image_width || !desc->image_height)
         throw error(CL_INVALID_IMAGE_SIZE);

      if (all_of([=](const device &dev) {
               const size_t max = 1 << dev.max_image_levels_2d();
               return (desc->image_width > max ||
                       desc->image_height > max);
            }, ctx.devices()))
         throw error(CL_INVALID_IMAGE_SIZE);

      return new image2d(ctx, flags, format,
                         desc->image_width, desc->image_height,
                         row_pitch, host_ptr);

   case CL_MEM_OBJECT_IMAGE3D: {
      if (!desc->image_width || !desc->image_height || !desc->image_depth)
         throw error(CL_INVALID_IMAGE_SIZE);

      if (all_of([=](const device &dev) {
               const size_t max = 1 << dev.max_image_levels_3d();
               return (desc->image_width > max ||
                       desc->image_height > max ||
                       desc->image_depth > max);
            }, ctx.devices()))
         throw error(CL_INVALID_IMAGE_SIZE);

      const size_t slice_pitch = desc->image_slice_pitch ?
         desc->image_slice_pitch : row_pitch * desc->image_height;

      return new image3d(ctx, flags, format,
                         desc->image_width, desc->image_height,
                         desc->image_depth, row_pitch,
                         slice_pitch, host_ptr);
   }

   case CL_MEM_OBJECT_IMAGE1D:
   case CL_MEM_OBJECT_IMAGE1D_ARRAY:
   case CL_MEM_OBJECT_IMAGE1D_BUFFER:
   case CL_MEM_OBJECT_IMAGE2D_ARRAY:
      // XXX - Not implemented.
      throw error(CL_IMAGE_FORMAT_NOT_SUPPORTED);

   default:
      throw error(CL_INVALID_IMAGE_DESCRIPTOR);
   }

} catch (error &e) {
   ret_error(r_errcode, e);
   return NULL;
}

CLOVER_API cl_mem
clCreateImage2D(cl_context d_ctx, cl_mem_flags d_flags,
                const cl_image_format *format,
                size_t width, size_t height, size_t row_pitch,
                void *host_ptr, cl_int *r_errcode) {
   const cl_image_desc desc = { CL_MEM_OBJECT_IMAGE2D, width, height, 0, 0,
                                row_pitch, 0, 0, 0, NULL };

   return clCreateImage(d_ctx, d_flags, format, &desc, host_ptr, r_errcode);
}

CLOVER_API cl_mem
clCreateImage3D(cl_context d_ctx, cl_mem_flags d_flags,
                const cl_image_format *format,
                size_t width, size_t height, size_t depth,
                size_t row_pitch, size_t slice_pitch,
                void *host_ptr, cl_int *r_errcode) {
   const cl_image_desc desc = { CL_MEM_OBJECT_IMAGE3D, width, height, depth, 0,
                                row_pitch, slice_pitch, 0, 0, NULL };

   return clCreateImage(d_ctx, d_flags, format, &desc, host_ptr, r_errcode);
}

CLOVER_API cl_int
clGetSupportedImageFormats(cl_context d_ctx, cl_mem_flags flags,
                           cl_mem_object_type type, cl_uint count,
                           cl_image_format *r_buf, cl_uint *r_count) try {
   auto &ctx = obj(d_ctx);
   auto formats = supported_formats(ctx, type);

   validate_flags(NULL, flags, false);

   if (r_buf && !count)
      throw error(CL_INVALID_VALUE);

   if (r_buf)
      std::copy_n(formats.begin(),
                  std::min((cl_uint)formats.size(), count),
                  r_buf);

   if (r_count)
      *r_count = formats.size();

   return CL_SUCCESS;

} catch (error &e) {
   return e.get();
}

CLOVER_API cl_int
clGetMemObjectInfo(cl_mem d_mem, cl_mem_info param,
                   size_t size, void *r_buf, size_t *r_size) try {
   property_buffer buf { r_buf, size, r_size };
   auto &mem = obj(d_mem);

   switch (param) {
   case CL_MEM_TYPE:
      buf.as_scalar<cl_mem_object_type>() = mem.type();
      break;

   case CL_MEM_FLAGS:
      buf.as_scalar<cl_mem_flags>() = mem.flags();
      break;

   case CL_MEM_SIZE:
      buf.as_scalar<size_t>() = mem.size();
      break;

   case CL_MEM_HOST_PTR:
      buf.as_scalar<void *>() = mem.host_ptr();
      break;

   case CL_MEM_MAP_COUNT:
      buf.as_scalar<cl_uint>() = 0;
      break;

   case CL_MEM_REFERENCE_COUNT:
      buf.as_scalar<cl_uint>() = mem.ref_count();
      break;

   case CL_MEM_CONTEXT:
      buf.as_scalar<cl_context>() = desc(mem.context());
      break;

   case CL_MEM_ASSOCIATED_MEMOBJECT: {
      sub_buffer *sub = dynamic_cast<sub_buffer *>(&mem);
      buf.as_scalar<cl_mem>() = (sub ? desc(sub->parent()) : NULL);
      break;
   }
   case CL_MEM_OFFSET: {
      sub_buffer *sub = dynamic_cast<sub_buffer *>(&mem);
      buf.as_scalar<size_t>() = (sub ? sub->offset() : 0);
      break;
   }
   case CL_MEM_USES_SVM_POINTER:
   case CL_MEM_USES_SVM_POINTER_ARM: {
      // with system SVM all host ptrs are SVM pointers
      // TODO: once we support devices with lower levels of SVM, we have to
      // check the ptr in more detail
      const bool system_svm = all_of(std::mem_fn(&device::has_system_svm),
                                     mem.context().devices());
      buf.as_scalar<cl_bool>() = mem.host_ptr() && system_svm;
      break;
   }
   default:
      throw error(CL_INVALID_VALUE);
   }

   return CL_SUCCESS;

} catch (error &e) {
   return e.get();
}

CLOVER_API cl_int
clGetImageInfo(cl_mem d_mem, cl_image_info param,
               size_t size, void *r_buf, size_t *r_size) try {
   property_buffer buf { r_buf, size, r_size };
   auto &img = obj<image>(d_mem);

   switch (param) {
   case CL_IMAGE_FORMAT:
      buf.as_scalar<cl_image_format>() = img.format();
      break;

   case CL_IMAGE_ELEMENT_SIZE:
      buf.as_scalar<size_t>() = 0;
      break;

   case CL_IMAGE_ROW_PITCH:
      buf.as_scalar<size_t>() = img.row_pitch();
      break;

   case CL_IMAGE_SLICE_PITCH:
      buf.as_scalar<size_t>() = img.slice_pitch();
      break;

   case CL_IMAGE_WIDTH:
      buf.as_scalar<size_t>() = img.width();
      break;

   case CL_IMAGE_HEIGHT:
      buf.as_scalar<size_t>() = img.height();
      break;

   case CL_IMAGE_DEPTH:
      buf.as_scalar<size_t>() = img.depth();
      break;

   default:
      throw error(CL_INVALID_VALUE);
   }

   return CL_SUCCESS;

} catch (error &e) {
   return e.get();
}

CLOVER_API cl_int
clRetainMemObject(cl_mem d_mem) try {
   obj(d_mem).retain();
   return CL_SUCCESS;

} catch (error &e) {
   return e.get();
}

CLOVER_API cl_int
clReleaseMemObject(cl_mem d_mem) try {
   if (obj(d_mem).release())
      delete pobj(d_mem);

   return CL_SUCCESS;

} catch (error &e) {
   return e.get();
}

CLOVER_API cl_int
clSetMemObjectDestructorCallback(cl_mem d_mem,
                                 void (CL_CALLBACK *pfn_notify)(cl_mem, void *),
                                 void *user_data) try {
   auto &mem = obj(d_mem);

   if (!pfn_notify)
      return CL_INVALID_VALUE;

   mem.destroy_notify([=]{ pfn_notify(d_mem, user_data); });

   return CL_SUCCESS;

} catch (error &e) {
   return e.get();
}

CLOVER_API void *
clSVMAlloc(cl_context d_ctx,
           cl_svm_mem_flags flags,
           size_t size,
           unsigned int alignment) try {
   auto &ctx = obj(d_ctx);
   validate_flags(NULL, flags, true);

   if (!size ||
       size > fold(minimum(), cl_ulong(ULONG_MAX),
                   map(std::mem_fn(&device::max_mem_alloc_size), ctx.devices())))
      return nullptr;

   if (!util_is_power_of_two_or_zero(alignment))
      return nullptr;

   if (!alignment)
      alignment = 0x80; // sizeof(long16)

   bool can_emulate = all_of(std::mem_fn(&device::has_system_svm), ctx.devices());
   if (can_emulate) {
      // we can ignore all the flags as it's not required to honor them.
      void *ptr = nullptr;
      if (alignment < sizeof(void*))
         alignment = sizeof(void*);
      posix_memalign(&ptr, alignment, size);
      return ptr;
   }

   CLOVER_NOT_SUPPORTED_UNTIL("2.0");
   return nullptr;

} catch (error &e) {
   return nullptr;
}

CLOVER_API void
clSVMFree(cl_context d_ctx,
          void *svm_pointer) try {
   auto &ctx = obj(d_ctx);
   bool can_emulate = all_of(std::mem_fn(&device::has_system_svm), ctx.devices());

   if (can_emulate)
      return free(svm_pointer);

   CLOVER_NOT_SUPPORTED_UNTIL("2.0");

} catch (error &e) {
}
