#include "pipe/p_context.h"
#include "util/u_surface.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h"
#include "util/u_memory.h"

void u_default_buffer_subdata(struct pipe_context *pipe,
                              struct pipe_resource *resource,
                              unsigned usage, unsigned offset,
                              unsigned size, const void *data)
{
   struct pipe_transfer *transfer = NULL;
   struct pipe_box box;
   uint8_t *map = NULL;

   assert(!(usage & PIPE_TRANSFER_READ));

   /* the write flag is implicit by the nature of buffer_subdata */
   usage |= PIPE_TRANSFER_WRITE;

   /* buffer_subdata implicitly discards the rewritten buffer range */
   if (offset == 0 && size == resource->width0) {
      usage |= PIPE_TRANSFER_DISCARD_WHOLE_RESOURCE;
   } else {
      usage |= PIPE_TRANSFER_DISCARD_RANGE;
   }

   u_box_1d(offset, size, &box);

   map = pipe->transfer_map(pipe, resource, 0, usage, &box, &transfer);
   if (!map)
      return;

   memcpy(map, data, size);
   pipe_transfer_unmap(pipe, transfer);
}

void u_default_texture_subdata(struct pipe_context *pipe,
                               struct pipe_resource *resource,
                               unsigned level,
                               unsigned usage,
                               const struct pipe_box *box,
                               const void *data,
                               unsigned stride,
                               unsigned layer_stride)
{
   struct pipe_transfer *transfer = NULL;
   const uint8_t *src_data = data;
   uint8_t *map = NULL;

   assert(!(usage & PIPE_TRANSFER_READ));

   /* the write flag is implicit by the nature of texture_subdata */
   usage |= PIPE_TRANSFER_WRITE;

   /* texture_subdata implicitly discards the rewritten buffer range */
   usage |= PIPE_TRANSFER_DISCARD_RANGE;

   map = pipe->transfer_map(pipe,
                            resource,
                            level,
                            usage,
                            box, &transfer);
   if (!map)
      return;

   util_copy_box(map,
                 resource->format,
                 transfer->stride, /* bytes */
                 transfer->layer_stride, /* bytes */
                 0, 0, 0,
                 box->width,
                 box->height,
                 box->depth,
                 src_data,
                 stride,       /* bytes */
                 layer_stride, /* bytes */
                 0, 0, 0);

   pipe_transfer_unmap(pipe, transfer);
}


boolean u_default_resource_get_handle(struct pipe_screen *screen,
                                      struct pipe_resource *resource,
                                      struct winsys_handle *handle)
{
   return FALSE;
}



void u_default_transfer_flush_region( struct pipe_context *pipe,
                                      struct pipe_transfer *transfer,
                                      const struct pipe_box *box)
{
   /* This is a no-op implementation, nothing to do.
    */
}

void u_default_transfer_unmap( struct pipe_context *pipe,
                               struct pipe_transfer *transfer )
{
}


static inline struct u_resource *
u_resource( struct pipe_resource *res )
{
   return (struct u_resource *)res;
}

boolean u_resource_get_handle_vtbl(struct pipe_screen *screen,
                                   struct pipe_context *ctx,
                                   struct pipe_resource *resource,
                                   struct winsys_handle *handle,
                                   unsigned usage)
{
   struct u_resource *ur = u_resource(resource);
   return ur->vtbl->resource_get_handle(screen, resource, handle);
}

void u_resource_destroy_vtbl(struct pipe_screen *screen,
                             struct pipe_resource *resource)
{
   struct u_resource *ur = u_resource(resource);
   ur->vtbl->resource_destroy(screen, resource);
}

void *u_transfer_map_vtbl(struct pipe_context *context,
                          struct pipe_resource *resource,
                          unsigned level,
                          unsigned usage,
                          const struct pipe_box *box,
                          struct pipe_transfer **transfer)
{
   struct u_resource *ur = u_resource(resource);
   return ur->vtbl->transfer_map(context, resource, level, usage, box,
                                 transfer);
}

void u_transfer_flush_region_vtbl( struct pipe_context *pipe,
                                   struct pipe_transfer *transfer,
                                   const struct pipe_box *box)
{
   struct u_resource *ur = u_resource(transfer->resource);
   ur->vtbl->transfer_flush_region(pipe, transfer, box);
}

void u_transfer_unmap_vtbl( struct pipe_context *pipe,
                            struct pipe_transfer *transfer )
{
   struct u_resource *ur = u_resource(transfer->resource);
   ur->vtbl->transfer_unmap(pipe, transfer);
}
