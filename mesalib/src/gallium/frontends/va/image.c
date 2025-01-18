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

#include "util/u_memory.h"
#include "util/u_handle_table.h"
#include "util/u_surface.h"
#include "util/u_video.h"
#include "util/u_process.h"

#include "vl/vl_winsys.h"
#include "vl/vl_video_buffer.h"

#include "va_private.h"

static const VAImageFormat formats[] =
{
   {VA_FOURCC('N','V','1','2')},
   {VA_FOURCC('P','0','1','0')},
   {VA_FOURCC('P','0','1','2')},
   {VA_FOURCC('P','0','1','6')},
   {VA_FOURCC('I','4','2','0')},
   {VA_FOURCC('Y','V','1','2')},
   {VA_FOURCC('Y','U','Y','V')},
   {VA_FOURCC('Y','U','Y','2')},
   {VA_FOURCC('U','Y','V','Y')},
   {VA_FOURCC('Y','8','0','0')},
   {VA_FOURCC('4','4','4','P')},
   {VA_FOURCC('4','2','2','V')},
   {VA_FOURCC('R','G','B','P')},
   {.fourcc = VA_FOURCC('B','G','R','A'), .byte_order = VA_LSB_FIRST, 32, 32,
    0x00ff0000, 0x0000ff00, 0x000000ff, 0xff000000},
   {.fourcc = VA_FOURCC('R','G','B','A'), .byte_order = VA_LSB_FIRST, 32, 32,
    0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000},
   {.fourcc = VA_FOURCC('A','R','G','B'), .byte_order = VA_LSB_FIRST, 32, 32,
    0x000000ff, 0x0000ff00, 0x00ff0000, 0xff000000},
   {.fourcc = VA_FOURCC('B','G','R','X'), .byte_order = VA_LSB_FIRST, 32, 24,
    0x00ff0000, 0x0000ff00, 0x000000ff, 0x00000000},
   {.fourcc = VA_FOURCC('R','G','B','X'), .byte_order = VA_LSB_FIRST, 32, 24,
    0x000000ff, 0x0000ff00, 0x00ff0000, 0x00000000},
   {.fourcc = VA_FOURCC('A','R','3','0'), .byte_order = VA_LSB_FIRST, 32, 30,
    0x3ff00000, 0x000ffc00, 0x000003ff, 0x30000000},
   {.fourcc = VA_FOURCC('A','B','3','0'), .byte_order = VA_LSB_FIRST, 32, 30,
    0x000003ff, 0x000ffc00, 0x3ff00000, 0x30000000},
   {.fourcc = VA_FOURCC('X','R','3','0'), .byte_order = VA_LSB_FIRST, 32, 30,
    0x3ff00000, 0x000ffc00, 0x000003ff, 0x00000000},
   {.fourcc = VA_FOURCC('X','B','3','0'), .byte_order = VA_LSB_FIRST, 32, 30,
    0x000003ff, 0x000ffc00, 0x3ff00000, 0x00000000},
};

static void
vlVaVideoSurfaceSize(vlVaSurface *p_surf, int component,
                     unsigned *width, unsigned *height)
{
   *width = p_surf->templat.width;
   *height = p_surf->templat.height;

   vl_video_buffer_adjust_size(width, height, component,
                               pipe_format_to_chroma_format(p_surf->templat.buffer_format),
                               p_surf->templat.interlaced);
}

VAStatus
vlVaQueryImageFormats(VADriverContextP ctx, VAImageFormat *format_list, int *num_formats)
{
   struct pipe_screen *pscreen;
   enum pipe_format format;
   int i;

   STATIC_ASSERT(ARRAY_SIZE(formats) == VL_VA_MAX_IMAGE_FORMATS);

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!(format_list && num_formats))
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   *num_formats = 0;
   pscreen = VL_VA_PSCREEN(ctx);
   for (i = 0; i < ARRAY_SIZE(formats); ++i) {
      format = VaFourccToPipeFormat(formats[i].fourcc);
      if (pscreen->is_video_format_supported(pscreen, format,
          PIPE_VIDEO_PROFILE_UNKNOWN,
          PIPE_VIDEO_ENTRYPOINT_BITSTREAM))
         format_list[(*num_formats)++] = formats[i];
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaCreateImage(VADriverContextP ctx, VAImageFormat *format, int width, int height, VAImage *image)
{
   VAStatus status;
   vlVaDriver *drv;
   VAImage *img;
   int w, h;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!(format && image && width && height))
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   drv = VL_VA_DRIVER(ctx);

   img = CALLOC(1, sizeof(VAImage));
   if (!img)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   mtx_lock(&drv->mutex);
   img->image_id = handle_table_add(drv->htab, img);
   mtx_unlock(&drv->mutex);

   img->format = *format;
   img->width = width;
   img->height = height;
   w = align(width, 2);
   h = align(height, 2);

   switch (format->fourcc) {
   case VA_FOURCC('N','V','1','2'):
      img->num_planes = 2;
      img->pitches[0] = w;
      img->offsets[0] = 0;
      img->pitches[1] = w;
      img->offsets[1] = w * h;
      img->data_size  = w * h * 3 / 2;
      break;

   case VA_FOURCC('P','0','1','0'):
   case VA_FOURCC('P','0','1','2'):
   case VA_FOURCC('P','0','1','6'):
      img->num_planes = 2;
      img->pitches[0] = w * 2;
      img->offsets[0] = 0;
      img->pitches[1] = w * 2;
      img->offsets[1] = w * h * 2;
      img->data_size  = w * h * 3;
      break;

   case VA_FOURCC('I','4','2','0'):
   case VA_FOURCC('Y','V','1','2'):
      img->num_planes = 3;
      img->pitches[0] = w;
      img->offsets[0] = 0;
      img->pitches[1] = w / 2;
      img->offsets[1] = w * h;
      img->pitches[2] = w / 2;
      img->offsets[2] = w * h * 5 / 4;
      img->data_size  = w * h * 3 / 2;
      break;

   case VA_FOURCC('U','Y','V','Y'):
   case VA_FOURCC('Y','U','Y','V'):
   case VA_FOURCC('Y','U','Y','2'):
      img->num_planes = 1;
      img->pitches[0] = w * 2;
      img->offsets[0] = 0;
      img->data_size  = w * h * 2;
      break;

   case VA_FOURCC('B','G','R','A'):
   case VA_FOURCC('R','G','B','A'):
   case VA_FOURCC('A','R','G','B'):
   case VA_FOURCC('B','G','R','X'):
   case VA_FOURCC('R','G','B','X'):
   case VA_FOURCC('A','R','3','0'):
   case VA_FOURCC('A','B','3','0'):
   case VA_FOURCC('X','R','3','0'):
   case VA_FOURCC('X','B','3','0'):
      img->num_planes = 1;
      img->pitches[0] = w * 4;
      img->offsets[0] = 0;
      img->data_size  = w * h * 4;
      break;

   case VA_FOURCC('Y','8','0','0'):
      img->num_planes = 1;
      img->pitches[0] = w;
      img->offsets[0] = 0;
      img->data_size  = w * h;
      break;

   case VA_FOURCC('4','4','4', 'P'):
   case VA_FOURCC('R','G','B', 'P'):
      img->num_planes = 3;
      img->offsets[0] = 0;
      img->offsets[1] = w * h;
      img->offsets[2] = w * h * 2;
      img->pitches[0] = w;
      img->pitches[1] = w;
      img->pitches[2] = w;
      img->data_size  = w * h * 3;
      break;

   case VA_FOURCC('4','2','2', 'V'):
      img->num_planes = 3;
      img->offsets[0] = 0;
      img->offsets[1] = w * h;
      img->offsets[2] = w * h * 3 / 2;
      img->pitches[0] = w;
      img->pitches[1] = w;
      img->pitches[2] = w;
      img->data_size  = w * h * 2;
      break;

   default:
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
   }

   status =  vlVaCreateBuffer(ctx, 0, VAImageBufferType,
                           align(img->data_size, 16),
                           1, NULL, &img->buf);
   if (status != VA_STATUS_SUCCESS)
      return status;
   *image = *img;

   return status;
}

VAStatus
vlVaDeriveImage(VADriverContextP ctx, VASurfaceID surface, VAImage *image)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   vlVaBuffer *img_buf;
   VAImage *img = NULL;
   VAStatus status;
   struct pipe_screen *screen;
   struct pipe_resource *buf_resources[VL_NUM_COMPONENTS];
   struct pipe_video_buffer *new_buffer = NULL;
   int w;
   int h;
   int i;
   unsigned stride = 0;
   unsigned offset = 0;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   screen = VL_VA_PSCREEN(ctx);

   if (!screen)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   surf = handle_table_get(drv->htab, surface);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      status = VA_STATUS_ERROR_INVALID_SURFACE;
      goto exit_on_error;
   }

   if (surf->buffer->interlaced) {
      status = VA_STATUS_ERROR_OPERATION_FAILED;
      goto exit_on_error;
   }

   if (util_format_get_num_planes(surf->buffer->buffer_format) >= 2 &&
       (!screen->get_video_param(screen, PIPE_VIDEO_PROFILE_UNKNOWN,
                                         PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
                                         PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP) ||
        !surf->buffer->contiguous_planes)) {
      status = VA_STATUS_ERROR_OPERATION_FAILED;
      goto exit_on_error;
   }

   memset(buf_resources, 0, sizeof(buf_resources));
   surf->buffer->get_resources(surf->buffer, buf_resources);

   if (!buf_resources[0]) {
      status = VA_STATUS_ERROR_ALLOCATION_FAILED;
      goto exit_on_error;
   }

   img = CALLOC(1, sizeof(VAImage));
   if (!img) {
      status = VA_STATUS_ERROR_ALLOCATION_FAILED;
      goto exit_on_error;
   }

   img->format.fourcc = PipeFormatToVaFourcc(surf->buffer->buffer_format);
   img->buf = VA_INVALID_ID;
   /* Use the visible dimensions. */
   img->width = surf->templat.width;
   img->height = surf->templat.height;
   img->num_palette_entries = 0;
   img->entry_bytes = 0;
   /* Image data size is computed using internal dimensions. */
   w = align(surf->buffer->width, 2);
   h = align(surf->buffer->height, 2);

   for (i = 0; i < ARRAY_SIZE(formats); ++i) {
      if (img->format.fourcc == formats[i].fourcc) {
         img->format = formats[i];
         break;
      }
   }

   if (screen->resource_get_info) {
      screen->resource_get_info(screen, buf_resources[0], &stride,
                                &offset);
      if (!stride)
         offset = 0;
   }

   img->num_planes = 1;
   img->offsets[0] = offset;

   switch (img->format.fourcc) {
   case VA_FOURCC('U','Y','V','Y'):
   case VA_FOURCC('Y','U','Y','V'):
      img->pitches[0] = stride > 0 ? stride : w * 2;
      assert(img->pitches[0] >= (w * 2));
      img->data_size  = img->pitches[0] * h;
      break;

   case VA_FOURCC('B','G','R','A'):
   case VA_FOURCC('R','G','B','A'):
   case VA_FOURCC('B','G','R','X'):
   case VA_FOURCC('R','G','B','X'):
   case VA_FOURCC('A','R','3','0'):
   case VA_FOURCC('A','B','3','0'):
   case VA_FOURCC('X','R','3','0'):
   case VA_FOURCC('X','B','3','0'):
      img->pitches[0] = stride > 0 ? stride : w * 4;
      assert(img->pitches[0] >= (w * 4));
      img->data_size  = img->pitches[0] * h;
      break;

   case VA_FOURCC('N','V','1','2'):
   case VA_FOURCC('P','0','1','0'):
   case VA_FOURCC('P','0','1','2'):
   case VA_FOURCC('P','0','1','6'):
   {
      /* In some gallium platforms, the stride and offset are different*/
      /* for the Y and UV planes, query them independently.*/
      if (screen->resource_get_info) {
         /* resource_get_info is called above for buf_resources[0] and */
         /* saved results in stride, offset, reuse those values to avoid a new call to: */
         /* screen->resource_get_info(screen, buf_resources[0], &img->pitches[0],*/
         /*                         &img->offsets[0]);*/
         img->pitches[0] = stride;
         img->offsets[0] = offset;

         screen->resource_get_info(screen, buf_resources[1], &img->pitches[1],
                                 &img->offsets[1]);
         if (!img->pitches[1])
               img->offsets[1] = 0;
      }

      img->num_planes = 2;
      if(screen->resource_get_info) {
         img->data_size  = (img->pitches[0] * h) + (img->pitches[1] * h / 2);
      } else {
         /* Use stride = w as default if screen->resource_get_info was not available */
         img->pitches[0] = w;
         img->pitches[1] = w;
         img->offsets[1] = w * h;
         img->data_size  = w * h * 3 / 2;
      }
   } break;
   default:
      /* VaDeriveImage only supports contiguous planes. But there is now a
         more generic api vlVaExportSurfaceHandle. */
      status = VA_STATUS_ERROR_OPERATION_FAILED;
      goto exit_on_error;
   }

   img_buf = CALLOC(1, sizeof(vlVaBuffer));
   if (!img_buf) {
      status = VA_STATUS_ERROR_ALLOCATION_FAILED;
      goto exit_on_error;
   }

   img->image_id = handle_table_add(drv->htab, img);

   img_buf->type = VAImageBufferType;
   img_buf->size = img->data_size;
   img_buf->num_elements = 1;

   pipe_resource_reference(&img_buf->derived_surface.resource, buf_resources[0]);
   img_buf->derived_image_buffer = new_buffer;

   if (surf->ctx)
      img_buf->derived_surface.entrypoint = surf->ctx->templat.entrypoint;

   img->buf = handle_table_add(VL_VA_DRIVER(ctx)->htab, img_buf);
   mtx_unlock(&drv->mutex);

   *image = *img;

   return VA_STATUS_SUCCESS;

exit_on_error:
   FREE(img);
   mtx_unlock(&drv->mutex);
   return status;
}

VAStatus
vlVaDestroyImage(VADriverContextP ctx, VAImageID image)
{
   vlVaDriver *drv;
   VAImage  *vaimage;
   VAStatus status;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);
   vaimage = handle_table_get(drv->htab, image);
   if (!vaimage) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   handle_table_remove(VL_VA_DRIVER(ctx)->htab, image);
   mtx_unlock(&drv->mutex);
   status = vlVaDestroyBuffer(ctx, vaimage->buf);
   FREE(vaimage);
   return status;
}

VAStatus
vlVaSetImagePalette(VADriverContextP ctx, VAImageID image, unsigned char *palette)
{
   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   return VA_STATUS_ERROR_UNIMPLEMENTED;
}

VAStatus
vlVaGetImage(VADriverContextP ctx, VASurfaceID surface, int x, int y,
             unsigned int width, unsigned int height, VAImageID image)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   vlVaBuffer *img_buf;
   VAImage *vaimage;
   struct pipe_resource *view_resources[VL_NUM_COMPONENTS];
   enum pipe_format format;
   uint8_t *data[3];
   unsigned pitches[3], i, j;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   mtx_lock(&drv->mutex);
   surf = handle_table_get(drv->htab, surface);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   vaimage = handle_table_get(drv->htab, image);
   if (!vaimage) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   if (x < 0 || y < 0) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_PARAMETER;
   }

   if (x + width > surf->templat.width ||
       y + height > surf->templat.height) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_PARAMETER;
   }

   if (width > vaimage->width ||
       height > vaimage->height) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_PARAMETER;
   }

   img_buf = handle_table_get(drv->htab, vaimage->buf);
   if (!img_buf) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   format = VaFourccToPipeFormat(vaimage->format.fourcc);
   if (format != surf->buffer->buffer_format) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
   }

   memset(view_resources, 0, sizeof(view_resources));
   surf->buffer->get_resources(surf->buffer, view_resources);

   for (i = 0; i < MIN2(vaimage->num_planes, 3); i++) {
      data[i] = ((uint8_t*)img_buf->data) + vaimage->offsets[i];
      pitches[i] = vaimage->pitches[i];
   }

   for (i = 0; i < vaimage->num_planes; i++) {
      unsigned box_w = align(width, 2);
      unsigned box_h = align(height, 2);
      unsigned box_x = x & ~1;
      unsigned box_y = y & ~1;
      if (!view_resources[i]) continue;
      vl_video_buffer_adjust_size(&box_w, &box_h, i,
                                  pipe_format_to_chroma_format(surf->templat.buffer_format),
                                  surf->templat.interlaced);
      vl_video_buffer_adjust_size(&box_x, &box_y, i,
                                  pipe_format_to_chroma_format(surf->templat.buffer_format),
                                  surf->templat.interlaced);
      for (j = 0; j < view_resources[i]->array_size; ++j) {
         struct pipe_box box;
         u_box_3d(box_x, box_y, j, box_w, box_h, 1, &box);
         struct pipe_transfer *transfer;
         uint8_t *map;
         map = drv->pipe->texture_map(drv->pipe, view_resources[i], 0,
                  PIPE_MAP_READ, &box, &transfer);
         if (!map) {
            mtx_unlock(&drv->mutex);
            return VA_STATUS_ERROR_OPERATION_FAILED;
         }
         util_copy_rect((uint8_t*)(data[i] + pitches[i] * j),
            view_resources[i]->format,
            pitches[i] * view_resources[i]->array_size, 0, 0,
            box.width, box.height, map, transfer->stride, 0, 0);
         pipe_texture_unmap(drv->pipe, transfer);
      }
   }
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaPutImage(VADriverContextP ctx, VASurfaceID surface, VAImageID image,
             int src_x, int src_y, unsigned int src_width, unsigned int src_height,
             int dest_x, int dest_y, unsigned int dest_width, unsigned int dest_height)
{
   vlVaDriver *drv;
   vlVaSurface *surf;
   vlVaBuffer *img_buf;
   VAImage *vaimage;
   struct pipe_resource *view_resources[VL_NUM_COMPONENTS];
   enum pipe_format format;
   uint8_t *data[3];
   unsigned pitches[3], i, j;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   mtx_lock(&drv->mutex);

   surf = handle_table_get(drv->htab, surface);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   vaimage = handle_table_get(drv->htab, image);
   if (!vaimage) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE;
   }

   img_buf = handle_table_get(drv->htab, vaimage->buf);
   if (!img_buf) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_BUFFER;
   }

   if (img_buf->derived_surface.resource) {
      /* Attempting to transfer derived image to surface */
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_UNIMPLEMENTED;
   }

   format = VaFourccToPipeFormat(vaimage->format.fourcc);
   if (format != surf->buffer->buffer_format) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_IMAGE_FORMAT;
   }

   memset(view_resources, 0, sizeof(view_resources));
   surf->buffer->get_resources(surf->buffer, view_resources);

   for (i = 0; i < MIN2(vaimage->num_planes, 3); i++) {
      data[i] = ((uint8_t*)img_buf->data) + vaimage->offsets[i];
      pitches[i] = vaimage->pitches[i];
   }

   for (i = 0; i < vaimage->num_planes; ++i) {
      unsigned width, height;
      struct pipe_resource *tex;

      if (!view_resources[i]) continue;
      tex = view_resources[i];

      vlVaVideoSurfaceSize(surf, i, &width, &height);
      for (j = 0; j < tex->array_size; ++j) {
         struct pipe_box dst_box;
         u_box_3d(0, 0, j, width, height, 1, &dst_box);
         drv->pipe->texture_subdata(drv->pipe, tex, 0,
                                    PIPE_MAP_WRITE, &dst_box,
                                    data[i] + pitches[i] * j,
                                    pitches[i] * view_resources[i]->array_size, 0);
      }
   }
   drv->pipe->flush(drv->pipe, NULL, 0);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}
