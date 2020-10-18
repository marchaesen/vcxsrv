/**************************************************************************
 *
 * Copyright 2010 Thomas Balling Sørensen & Orasanu Lucian.
 * Copyright 2014 Advanced Micro Devices, Inc.
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
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "pipe/p_screen.h"
#include "frontend/drm_driver.h"
#include "util/u_memory.h"
#include "util/u_handle_table.h"
#include "util/u_transfer.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

VAStatus
vlVaCreateBuffer(VADriverContextP ctx, VAContextID context, VABufferType type,
                 unsigned int size, unsigned int num_elements, void *data,
                 VABufferID *buf_id)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   buf = CALLOC(1, sizeof(vlVaBuffer));
   if (!buf)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   buf->type = type;
   buf->size = size;
   buf->num_elements = num_elements;
   buf->data = MALLOC(size * num_elements);

   if (!buf->data) {
      FREE(buf);
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   }

   if (data)
      memcpy(buf->data, data, size * num_elements);

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   *buf_id = handle_table_add(drv->htab, buf);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaBufferSetNumElements(VADriverContextP ctx, VABufferID buf_id,
                         unsigned int num_elements)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   buf = handle_table_get(drv->htab, buf_id);
   mtx_unlock(&drv->mutex);
   if (!buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (buf->derived_surface.resource)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   buf->data = REALLOC(buf->data, buf->size * buf->num_elements,
                       buf->size * num_elements);
   buf->num_elements = num_elements;

   if (!buf->data)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaMapBuffer(VADriverContextP ctx, VABufferID buf_id, void **pbuff)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!pbuff)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   mtx_lock(&drv->mutex);
   buf = handle_table_get(drv->htab, buf_id);
   if (!buf || buf->export_refcount > 0) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   if (buf->derived_surface.resource) {
      struct pipe_resource *resource;
      struct pipe_box box = {};

      resource = buf->derived_surface.resource;
      box.width = resource->width0;
      box.height = resource->height0;
      box.depth = resource->depth0;
      *pbuff = drv->pipe->transfer_map(drv->pipe, resource, 0, PIPE_MAP_WRITE,
                                       &box, &buf->derived_surface.transfer);
      mtx_unlock(&drv->mutex);

      if (!buf->derived_surface.transfer || !*pbuff)
         return VA_STATUS_ERROR_INVALID_BUFFER;

      if (buf->type == VAEncCodedBufferType) {
         ((VACodedBufferSegment*)buf->data)->buf = *pbuff;
         ((VACodedBufferSegment*)buf->data)->size = buf->coded_size;
         ((VACodedBufferSegment*)buf->data)->next = NULL;
         *pbuff = buf->data;
      }
   } else {
      mtx_unlock(&drv->mutex);
      *pbuff = buf->data;
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaUnmapBuffer(VADriverContextP ctx, VABufferID buf_id)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   buf = handle_table_get(drv->htab, buf_id);
   if (!buf || buf->export_refcount > 0) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   if (buf->derived_surface.resource) {
      if (!buf->derived_surface.transfer) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      pipe_buffer_unmap(drv->pipe, buf->derived_surface.transfer);
      buf->derived_surface.transfer = NULL;
   }
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaDestroyBuffer(VADriverContextP ctx, VABufferID buf_id)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   buf = handle_table_get(drv->htab, buf_id);
   if (!buf) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   if (buf->derived_surface.resource) {
      pipe_resource_reference(&buf->derived_surface.resource, NULL);

      if (buf->derived_image_buffer)
         buf->derived_image_buffer->destroy(buf->derived_image_buffer);
   }

   FREE(buf->data);
   FREE(buf);
   handle_table_remove(VL_VA_DRIVER(ctx)->htab, buf_id);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaBufferInfo(VADriverContextP ctx, VABufferID buf_id, VABufferType *type,
               unsigned int *size, unsigned int *num_elements)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   buf = handle_table_get(drv->htab, buf_id);
   mtx_unlock(&drv->mutex);
   if (!buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   *type = buf->type;
   *size = buf->size;
   *num_elements = buf->num_elements;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaAcquireBufferHandle(VADriverContextP ctx, VABufferID buf_id,
                        VABufferInfo *out_buf_info)
{
   vlVaDriver *drv;
   uint32_t i;
   uint32_t mem_type;
   vlVaBuffer *buf ;
   struct pipe_screen *screen;

   /* List of supported memory types, in preferred order. */
   static const uint32_t mem_types[] = {
      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME,
      0
   };

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   screen = VL_VA_PSCREEN(ctx);
   mtx_lock(&drv->mutex);
   buf = handle_table_get(VL_VA_DRIVER(ctx)->htab, buf_id);
   mtx_unlock(&drv->mutex);

   if (!buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   /* Only VA surface|image like buffers are supported for now .*/
   if (buf->type != VAImageBufferType)
      return VA_STATUS_ERROR_UNSUPPORTED_BUFFERTYPE;

   if (!out_buf_info)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (!out_buf_info->mem_type)
      mem_type = mem_types[0];
   else {
      mem_type = 0;
      for (i = 0; mem_types[i] != 0; i++) {
         if (out_buf_info->mem_type & mem_types[i]) {
            mem_type = out_buf_info->mem_type;
            break;
         }
      }
      if (!mem_type)
         return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
   }

   if (!buf->derived_surface.resource)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (buf->export_refcount > 0) {
      if (buf->export_state.mem_type != mem_type)
         return VA_STATUS_ERROR_INVALID_PARAMETER;
   } else {
      VABufferInfo * const buf_info = &buf->export_state;

      switch (mem_type) {
      case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME: {
         struct winsys_handle whandle;

         mtx_lock(&drv->mutex);
         drv->pipe->flush(drv->pipe, NULL, 0);

         memset(&whandle, 0, sizeof(whandle));
         whandle.type = WINSYS_HANDLE_TYPE_FD;

         if (!screen->resource_get_handle(screen, drv->pipe,
                                          buf->derived_surface.resource,
                                          &whandle, PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE)) {
            mtx_unlock(&drv->mutex);
            return VA_STATUS_ERROR_INVALID_BUFFER;
         }

         mtx_unlock(&drv->mutex);

         buf_info->handle = (intptr_t)whandle.handle;
         break;
      }
      default:
         return VA_STATUS_ERROR_UNSUPPORTED_MEMORY_TYPE;
      }

      buf_info->type = buf->type;
      buf_info->mem_type = mem_type;
      buf_info->mem_size = buf->num_elements * buf->size;
   }

   buf->export_refcount++;

   *out_buf_info = buf->export_state;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaReleaseBufferHandle(VADriverContextP ctx, VABufferID buf_id)
{
   vlVaDriver *drv;
   vlVaBuffer *buf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   buf = handle_table_get(drv->htab, buf_id);
   mtx_unlock(&drv->mutex);

   if (!buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (buf->export_refcount == 0)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (--buf->export_refcount == 0) {
      VABufferInfo * const buf_info = &buf->export_state;

      switch (buf_info->mem_type) {
      case VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME:
         close((intptr_t)buf_info->handle);
         break;
      default:
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      buf_info->mem_type = 0;
   }

   return VA_STATUS_SUCCESS;
}
