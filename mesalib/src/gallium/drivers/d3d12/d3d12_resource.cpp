/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_resource.h"

#include "d3d12_blit.h"
#include "d3d12_context.h"
#include "d3d12_format.h"
#include "d3d12_screen.h"
#include "d3d12_debug.h"

#include "pipebuffer/pb_bufmgr.h"
#include "util/slab.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/format/u_format_zs.h"

#include "frontend/sw_winsys.h"

#include <directx/d3d12.h>
#include <dxguids/dxguids.h>
#include <memory>

static bool
can_map_directly(struct pipe_resource *pres)
{
   return pres->bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED | PIPE_BIND_LINEAR) ||
          pres->target == PIPE_BUFFER;
}

static void
init_valid_range(struct d3d12_resource *res)
{
   if (can_map_directly(&res->base))
      util_range_init(&res->valid_buffer_range);
}

static void
d3d12_resource_destroy(struct pipe_screen *pscreen,
                       struct pipe_resource *presource)
{
   struct d3d12_resource *resource = d3d12_resource(presource);
   if (can_map_directly(presource))
      util_range_destroy(&resource->valid_buffer_range);
   if (resource->bo)
      d3d12_bo_unreference(resource->bo);
   FREE(resource);
}

static bool
resource_is_busy(struct d3d12_context *ctx,
                 struct d3d12_resource *res)
{
   bool busy = false;

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->batches); i++)
      busy |= d3d12_batch_has_references(&ctx->batches[i], res->bo);

   return busy;
}

void
d3d12_resource_wait_idle(struct d3d12_context *ctx,
                         struct d3d12_resource *res)
{
   if (d3d12_batch_has_references(d3d12_current_batch(ctx), res->bo)) {
      d3d12_flush_cmdlist_and_wait(ctx);
   } else {
      d3d12_foreach_submitted_batch(ctx, batch) {
         d3d12_reset_batch(ctx, batch, PIPE_TIMEOUT_INFINITE);
         if (!resource_is_busy(ctx, res))
            break;
      }
   }
}

void
d3d12_resource_release(struct d3d12_resource *resource)
{
   if (!resource->bo)
      return;
   d3d12_bo_unreference(resource->bo);
   resource->bo = NULL;
}

static bool
init_buffer(struct d3d12_screen *screen,
            struct d3d12_resource *res,
            const struct pipe_resource *templ)
{
   struct pb_desc buf_desc;
   struct pb_manager *bufmgr;
   struct pb_buffer *buf;

   /* Assert that we don't want to create a buffer with one of the emulated
    * formats, these are (currently) only supported when passing the vertex
    * element state */
   assert(templ->format == d3d12_emulated_vtx_format(templ->format));

   /* Don't use slab buffer manager for GPU writable buffers */
   bufmgr = templ->bind & PIPE_BIND_STREAM_OUTPUT ? screen->cache_bufmgr
                                                  : screen->slab_bufmgr;
   buf_desc.alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   buf_desc.usage = (pb_usage_flags)PB_USAGE_ALL;
   res->dxgi_format = DXGI_FORMAT_UNKNOWN;
   buf = bufmgr->create_buffer(bufmgr, templ->width0, &buf_desc);
   if (!buf)
      return false;
   res->bo = d3d12_bo_wrap_buffer(buf);

   return true;
}

static bool
init_texture(struct d3d12_screen *screen,
             struct d3d12_resource *res,
             const struct pipe_resource *templ)
{
   ID3D12Resource *d3d12_res;

   res->mip_levels = templ->last_level + 1;
   res->dxgi_format = d3d12_get_format(templ->format);

   D3D12_RESOURCE_DESC desc;
   desc.Format = res->dxgi_format;
   desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
   desc.Width = templ->width0;
   desc.Height = templ->height0;
   desc.DepthOrArraySize = templ->array_size;
   desc.MipLevels = templ->last_level + 1;

   desc.SampleDesc.Count = MAX2(templ->nr_samples, 1);
   desc.SampleDesc.Quality = 0; /* TODO: figure this one out */

   switch (templ->target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE1D;
      break;

   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      desc.DepthOrArraySize *= 6;
      /* fall-through */
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_RECT:
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
      break;

   case PIPE_TEXTURE_3D:
      desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
      desc.DepthOrArraySize = templ->depth0;
      break;

   default:
      unreachable("Invalid texture type");
   }

   desc.Flags = D3D12_RESOURCE_FLAG_NONE;

   if (templ->bind & PIPE_BIND_SHADER_BUFFER)
      desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

   if (templ->bind & PIPE_BIND_RENDER_TARGET)
      desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

   if (templ->bind & PIPE_BIND_DEPTH_STENCIL) {
      desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

      /* Sadly, we can't set D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE in the
       * case where PIPE_BIND_SAMPLER_VIEW isn't set, because that would
       * prevent us from using the resource with u_blitter, which requires
       * sneaking in sampler-usage throught the back-door.
       */
   }

   desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
   if (templ->bind & (PIPE_BIND_SCANOUT |
                      PIPE_BIND_SHARED | PIPE_BIND_LINEAR))
      desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

   D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;

   if (templ->bind & (PIPE_BIND_DISPLAY_TARGET |
                      PIPE_BIND_SCANOUT |
                      PIPE_BIND_SHARED))
      heap_type = D3D12_HEAP_TYPE_READBACK;
   else if (templ->usage == PIPE_USAGE_STAGING)
      heap_type = D3D12_HEAP_TYPE_UPLOAD;

   D3D12_HEAP_PROPERTIES heap_pris = screen->dev->GetCustomHeapProperties(0, heap_type);

   HRESULT hres = screen->dev->CreateCommittedResource(&heap_pris,
                                                   D3D12_HEAP_FLAG_NONE,
                                                   &desc,
                                                   D3D12_RESOURCE_STATE_COMMON,
                                                   NULL,
                                                   IID_PPV_ARGS(&d3d12_res));
   if (FAILED(hres))
      return false;

   if (screen->winsys && (templ->bind & (PIPE_BIND_DISPLAY_TARGET |
                                         PIPE_BIND_SCANOUT |
                                         PIPE_BIND_SHARED))) {
      struct sw_winsys *winsys = screen->winsys;
      res->dt = winsys->displaytarget_create(screen->winsys,
                                             res->base.bind,
                                             res->base.format,
                                             templ->width0,
                                             templ->height0,
                                             64, NULL,
                                             &res->dt_stride);
   }

   res->bo = d3d12_bo_wrap_res(d3d12_res, templ->format);

   return true;
}

static struct pipe_resource *
d3d12_resource_create(struct pipe_screen *pscreen,
                      const struct pipe_resource *templ)
{
   struct d3d12_screen *screen = d3d12_screen(pscreen);
   struct d3d12_resource *res = CALLOC_STRUCT(d3d12_resource);
   bool ret;

   res->base = *templ;

   if (D3D12_DEBUG_RESOURCE & d3d12_debug) {
      debug_printf("D3D12: Create %sresource %s@%d %dx%dx%d as:%d mip:%d\n",
                   templ->usage == PIPE_USAGE_STAGING ? "STAGING " :"",
                   util_format_name(templ->format), templ->nr_samples,
                   templ->width0, templ->height0, templ->depth0,
                   templ->array_size, templ->last_level);
   }

   pipe_reference_init(&res->base.reference, 1);
   res->base.screen = pscreen;

   if (templ->target == PIPE_BUFFER) {
      ret = init_buffer(screen, res, templ);
   } else {
      ret = init_texture(screen, res, templ);
   }

   if (!ret) {
      FREE(res);
      return NULL;
   }

   init_valid_range(res);

   return &res->base;
}

static struct pipe_resource *
d3d12_resource_from_handle(struct pipe_screen *pscreen,
                          const struct pipe_resource *templ,
                          struct winsys_handle *handle, unsigned usage)
{
   if (handle->type != WINSYS_HANDLE_TYPE_D3D12_RES)
      return NULL;

   struct d3d12_resource *res = CALLOC_STRUCT(d3d12_resource);
   if (!res)
      return NULL;

   res->base = *templ;
   pipe_reference_init(&res->base.reference, 1);
   res->base.screen = pscreen;
   res->dxgi_format = templ->target == PIPE_BUFFER ? DXGI_FORMAT_UNKNOWN :
                 d3d12_get_format(templ->format);
   res->bo = d3d12_bo_wrap_res((ID3D12Resource *)handle->com_obj, templ->format);
   init_valid_range(res);
   return &res->base;
}

static bool
d3d12_resource_get_handle(struct pipe_screen *pscreen,
                          struct pipe_context *pcontext,
                          struct pipe_resource *pres,
                          struct winsys_handle *handle,
                          unsigned usage)
{
   struct d3d12_resource *res = d3d12_resource(pres);

   if (handle->type != WINSYS_HANDLE_TYPE_D3D12_RES)
      return false;

   handle->com_obj = d3d12_resource_resource(res);
   return true;
}

void
d3d12_screen_resource_init(struct pipe_screen *pscreen)
{
   pscreen->resource_create = d3d12_resource_create;
   pscreen->resource_from_handle = d3d12_resource_from_handle;
   pscreen->resource_get_handle = d3d12_resource_get_handle;
   pscreen->resource_destroy = d3d12_resource_destroy;
}

unsigned int
get_subresource_id(struct d3d12_resource *res, unsigned resid,
                   unsigned z, unsigned base_level)
{
   unsigned resource_stride = res->base.last_level + 1;
   if (res->base.target == PIPE_TEXTURE_1D_ARRAY ||
       res->base.target == PIPE_TEXTURE_2D_ARRAY)
      resource_stride *= res->base.array_size;

   if (res->base.target == PIPE_TEXTURE_CUBE)
      resource_stride *= 6;

   if (res->base.target == PIPE_TEXTURE_CUBE_ARRAY)
      resource_stride *= 6 * res->base.array_size;

   unsigned layer_stride = res->base.last_level + 1;

   return resid * resource_stride + z * layer_stride +
         base_level;
}

static D3D12_TEXTURE_COPY_LOCATION
fill_texture_location(struct d3d12_resource *res,
                      struct d3d12_transfer *trans, unsigned resid, unsigned z)
{
   D3D12_TEXTURE_COPY_LOCATION tex_loc = {0};
   int subres = get_subresource_id(res, resid, z, trans->base.level);

   tex_loc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
   tex_loc.SubresourceIndex = subres;
   tex_loc.pResource = d3d12_resource_resource(res);
   return tex_loc;
}

static D3D12_TEXTURE_COPY_LOCATION
fill_buffer_location(struct d3d12_context *ctx,
                     struct d3d12_resource *res,
                     struct d3d12_resource *staging_res,
                     struct d3d12_transfer *trans,
                     unsigned depth,
                     unsigned resid, unsigned z)
{
   D3D12_TEXTURE_COPY_LOCATION buf_loc = {0};
   D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
   uint64_t offset = 0;
   auto descr = d3d12_resource_underlying(res, &offset)->GetDesc();
   ID3D12Device* dev = d3d12_screen(ctx->base.screen)->dev;

   unsigned sub_resid = get_subresource_id(res, resid, z, trans->base.level);
   dev->GetCopyableFootprints(&descr, sub_resid, 1, 0, &footprint, nullptr, nullptr, nullptr);

   buf_loc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
   buf_loc.pResource = d3d12_resource_underlying(staging_res, &offset);
   buf_loc.PlacedFootprint = footprint;
   buf_loc.PlacedFootprint.Offset += offset;

   buf_loc.PlacedFootprint.Footprint.Width = ALIGN(trans->base.box.width,
                                                   util_format_get_blockwidth(res->base.format));
   buf_loc.PlacedFootprint.Footprint.Height = ALIGN(trans->base.box.height,
                                                    util_format_get_blockheight(res->base.format));
   buf_loc.PlacedFootprint.Footprint.Depth = ALIGN(depth,
                                                   util_format_get_blockdepth(res->base.format));

   buf_loc.PlacedFootprint.Footprint.RowPitch = trans->base.stride;

   return buf_loc;
}

struct copy_info {
   struct d3d12_resource *dst;
   D3D12_TEXTURE_COPY_LOCATION dst_loc;
   UINT dst_x, dst_y, dst_z;
   struct d3d12_resource *src;
   D3D12_TEXTURE_COPY_LOCATION src_loc;
   D3D12_BOX *src_box;
};


static void
copy_texture_region(struct d3d12_context *ctx,
                    struct copy_info& info)
{
   auto batch = d3d12_current_batch(ctx);

   d3d12_batch_reference_resource(batch, info.src);
   d3d12_batch_reference_resource(batch, info.dst);

   d3d12_transition_resource_state(ctx, info.src, D3D12_RESOURCE_STATE_COPY_SOURCE);
   d3d12_transition_resource_state(ctx, info.dst, D3D12_RESOURCE_STATE_COPY_DEST);
   d3d12_apply_resource_states(ctx);
   ctx->cmdlist->CopyTextureRegion(&info.dst_loc, info.dst_x, info.dst_y, info.dst_z,
                                   &info.src_loc, info.src_box);
}

static void
transfer_buf_to_image_part(struct d3d12_context *ctx,
                           struct d3d12_resource *res,
                           struct d3d12_resource *staging_res,
                           struct d3d12_transfer *trans,
                           int z, int depth, int start_z, int dest_z,
                           int resid)
{
   if (D3D12_DEBUG_RESOURCE & d3d12_debug) {
      debug_printf("D3D12: Copy %dx%dx%d + %dx%dx%d from buffer %s to image %s\n",
                   trans->base.box.x, trans->base.box.y, trans->base.box.z,
                   trans->base.box.width, trans->base.box.height, trans->base.box.depth,
                   util_format_name(staging_res->base.format),
                   util_format_name(res->base.format));
   }

   struct copy_info copy_info;
   copy_info.src = staging_res;
   copy_info.src_loc = fill_buffer_location(ctx, res, staging_res, trans, depth, resid, z);
   copy_info.src_loc.PlacedFootprint.Offset = (z  - start_z) * trans->base.layer_stride;
   copy_info.src_box = nullptr;
   copy_info.dst = res;
   copy_info.dst_loc = fill_texture_location(res, trans, resid, z);
   copy_info.dst_x = trans->base.box.x;
   copy_info.dst_y = trans->base.box.y;
   copy_info.dst_z = res->base.target == PIPE_TEXTURE_CUBE ? 0 : dest_z;
   copy_info.src_box = nullptr;

   copy_texture_region(ctx, copy_info);
}

static bool
transfer_buf_to_image(struct d3d12_context *ctx,
                      struct d3d12_resource *res,
                      struct d3d12_resource *staging_res,
                      struct d3d12_transfer *trans, int resid)
{
   if (res->base.target == PIPE_TEXTURE_3D) {
      assert(resid == 0);
      transfer_buf_to_image_part(ctx, res, staging_res, trans,
                                 0, trans->base.box.depth, 0,
                                 trans->base.box.z, 0);
   } else {
      int num_layers = trans->base.box.depth;
      int start_z = trans->base.box.z;

      for (int z = start_z; z < start_z + num_layers; ++z) {
         transfer_buf_to_image_part(ctx, res, staging_res, trans,
                                           z, 1, start_z, 0, resid);
      }
   }
   return true;
}

static void
transfer_image_part_to_buf(struct d3d12_context *ctx,
                           struct d3d12_resource *res,
                           struct d3d12_resource *staging_res,
                           struct d3d12_transfer *trans,
                           unsigned resid, int z, int start_layer,
                           int start_box_z, int depth)
{
   struct pipe_box *box = &trans->base.box;
   D3D12_BOX src_box = {};

   struct copy_info copy_info;
   copy_info.src_box = nullptr;
   copy_info.src = res;
   copy_info.src_loc = fill_texture_location(res, trans, resid, z);
   copy_info.dst = staging_res;
   copy_info.dst_loc = fill_buffer_location(ctx, res, staging_res, trans,
                                            depth, resid, z);
   copy_info.dst_loc.PlacedFootprint.Offset = (z  - start_layer) * trans->base.layer_stride;
   copy_info.dst_x = copy_info.dst_y = copy_info.dst_z = 0;

   if (!util_texrange_covers_whole_level(&res->base, trans->base.level,
                                         box->x, box->y, start_box_z,
                                         box->width, box->height, depth)) {
      src_box.left = box->x;
      src_box.right = box->x + box->width;
      src_box.top = box->y;
      src_box.bottom = box->y + box->height;
      src_box.front = start_box_z;
      src_box.back = start_box_z + depth;
      copy_info.src_box = &src_box;
   }

   copy_texture_region(ctx, copy_info);
}

static bool
transfer_image_to_buf(struct d3d12_context *ctx,
                            struct d3d12_resource *res,
                            struct d3d12_resource *staging_res,
                            struct d3d12_transfer *trans,
                            unsigned resid)
{

   /* We only suppport loading from either an texture array
    * or a ZS texture, so either resid is zero, or num_layers == 1)
    */
   assert(resid == 0 || trans->base.box.depth == 1);

   if (D3D12_DEBUG_RESOURCE & d3d12_debug) {
      debug_printf("D3D12: Copy %dx%dx%d + %dx%dx%d from %s@%d to %s\n",
                   trans->base.box.x, trans->base.box.y, trans->base.box.z,
                   trans->base.box.width, trans->base.box.height, trans->base.box.depth,
                   util_format_name(res->base.format), resid,
                   util_format_name(staging_res->base.format));
   }

   struct pipe_resource *resolved_resource = nullptr;
   if (res->base.nr_samples > 1) {
      struct pipe_resource tmpl = res->base;
      tmpl.nr_samples = 0;
      resolved_resource = d3d12_resource_create(ctx->base.screen, &tmpl);
      struct pipe_blit_info resolve_info = {0};
      struct pipe_box box = {0,0,0, (int)res->base.width0, (int16_t)res->base.height0, (int16_t)res->base.depth0};
      resolve_info.dst.resource = resolved_resource;
      resolve_info.dst.box = box;
      resolve_info.dst.format = res->base.format;
      resolve_info.src.resource = &res->base;
      resolve_info.src.box = box;
      resolve_info.src.format = res->base.format;
      resolve_info.filter = PIPE_TEX_FILTER_NEAREST;
      resolve_info.mask = util_format_get_mask(tmpl.format);



      d3d12_blit(&ctx->base, &resolve_info);
      res = (struct d3d12_resource *)resolved_resource;
   }


   if (res->base.target == PIPE_TEXTURE_3D) {
      transfer_image_part_to_buf(ctx, res, staging_res, trans, resid,
                                 0, 0, trans->base.box.z, trans->base.box.depth);
   } else {
      int start_layer = trans->base.box.z;
      for (int z = start_layer; z < start_layer + trans->base.box.depth; ++z) {
         transfer_image_part_to_buf(ctx, res, staging_res, trans, resid,
                                    z, start_layer, 0, 1);
      }
   }

   pipe_resource_reference(&resolved_resource, NULL);

   return true;
}

static unsigned
linear_offset(int x, int y, int z, unsigned stride, unsigned layer_stride)
{
   return x +
          y * stride +
          z * layer_stride;
}

static D3D12_RANGE
linear_range(const struct pipe_box *box, unsigned stride, unsigned layer_stride)
{
   D3D12_RANGE range;

   range.Begin = linear_offset(box->x, box->y, box->z,
                               stride, layer_stride);
   range.End = linear_offset(box->x + box->width,
                             box->y + box->height - 1,
                             box->z + box->depth - 1,
                             stride, layer_stride);

   return range;
}

static bool
synchronize(struct d3d12_context *ctx,
            struct d3d12_resource *res,
            unsigned usage,
            D3D12_RANGE *range)
{
   assert(can_map_directly(&res->base));

   /* Check whether that range contains valid data; if not, we might not need to sync */
   if (!(usage & PIPE_MAP_UNSYNCHRONIZED) &&
       usage & PIPE_MAP_WRITE &&
       !util_ranges_intersect(&res->valid_buffer_range, range->Begin, range->End)) {
      usage |= PIPE_MAP_UNSYNCHRONIZED;
   }

   if (!(usage & PIPE_MAP_UNSYNCHRONIZED) && resource_is_busy(ctx, res)) {
      if (usage & PIPE_MAP_DONTBLOCK)
         return false;

      d3d12_resource_wait_idle(ctx, res);
   }

   if (usage & PIPE_MAP_WRITE)
      util_range_add(&res->base, &res->valid_buffer_range,
                     range->Begin, range->End);

   return true;
}

/* A wrapper to make sure local resources are freed and unmapped with
 * any exit path */
struct local_resource {
   local_resource(pipe_screen *s, struct pipe_resource *tmpl) :
      mapped(false)
   {
      res = d3d12_resource(d3d12_resource_create(s, tmpl));
   }

   ~local_resource() {
      if (res) {
         if (mapped)
            d3d12_bo_unmap(res->bo, nullptr);
         pipe_resource_reference((struct pipe_resource **)&res, NULL);
      }
   }

   void *
   map() {
      void *ptr;
      ptr = d3d12_bo_map(res->bo, nullptr);
      if (ptr)
         mapped = true;
      return ptr;
   }

   void unmap()
   {
      if (mapped)
         d3d12_bo_unmap(res->bo, nullptr);
      mapped = false;
   }

   operator struct d3d12_resource *() {
      return res;
   }

   bool operator !() {
      return !res;
   }
private:
   struct d3d12_resource *res;
   bool mapped;
};

/* Combined depth-stencil needs a special handling for reading back: DX handled
 * depth and stencil parts as separate resources and handles copying them only
 * by using seperate texture copy calls with different formats. So create two
 * buffers, read back both resources and interleave the data.
 */
static void
prepare_zs_layer_strides(struct d3d12_resource *res,
                         const struct pipe_box *box,
                         struct d3d12_transfer *trans)
{
   trans->base.stride = align(util_format_get_stride(res->base.format, box->width),
                              D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
   trans->base.layer_stride = util_format_get_2d_size(res->base.format,
                                                      trans->base.stride,
                                                      box->height);
}

static void *
read_zs_surface(struct d3d12_context *ctx, struct d3d12_resource *res,
                const struct pipe_box *box,
                struct d3d12_transfer *trans)
{
   pipe_screen *pscreen = ctx->base.screen;

   prepare_zs_layer_strides(res, box, trans);

   struct pipe_resource tmpl;
   memset(&tmpl, 0, sizeof tmpl);
   tmpl.target = PIPE_BUFFER;
   tmpl.format = PIPE_FORMAT_R32_UNORM;
   tmpl.bind = 0;
   tmpl.usage = PIPE_USAGE_STAGING;
   tmpl.flags = 0;
   tmpl.width0 = trans->base.layer_stride;
   tmpl.height0 = 1;
   tmpl.depth0 = 1;
   tmpl.array_size = 1;

   local_resource depth_buffer(pscreen, &tmpl);
   if (!depth_buffer) {
      debug_printf("Allocating staging buffer for depth failed\n");
      return NULL;
   }

   if (!transfer_image_to_buf(ctx, res, depth_buffer, trans, 0))
      return NULL;

   tmpl.format = PIPE_FORMAT_R8_UINT;

   local_resource stencil_buffer(pscreen, &tmpl);
   if (!stencil_buffer) {
      debug_printf("Allocating staging buffer for stencilfailed\n");
      return NULL;
   }

   if (!transfer_image_to_buf(ctx, res, stencil_buffer, trans, 1))
      return NULL;

   d3d12_flush_cmdlist_and_wait(ctx);

   void *depth_ptr = depth_buffer.map();
   if (!depth_ptr) {
      debug_printf("Mapping staging depth buffer failed\n");
      return NULL;
   }

   uint8_t *stencil_ptr =  (uint8_t *)stencil_buffer.map();
   if (!stencil_ptr) {
      debug_printf("Mapping staging stencil buffer failed\n");
      return NULL;
   }

   uint8_t *buf = (uint8_t *)malloc(trans->base.layer_stride);
   if (!buf)
      return NULL;

   trans->data = buf;

   switch (res->base.format) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      util_format_z24_unorm_s8_uint_pack_separate(buf, trans->base.stride,
                                                  (uint32_t *)depth_ptr, trans->base.stride,
                                                  stencil_ptr, trans->base.stride,
                                                  trans->base.box.width, trans->base.box.height);
      break;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      util_format_z32_float_s8x24_uint_pack_z_float(buf, trans->base.stride,
                                                    (float *)depth_ptr, trans->base.stride,
                                                    trans->base.box.width, trans->base.box.height);
      util_format_z32_float_s8x24_uint_pack_s_8uint(buf, trans->base.stride,
                                                    stencil_ptr, trans->base.stride,
                                                    trans->base.box.width, trans->base.box.height);
      break;
   default:
      unreachable("Unsupported depth steancil format");
   };

   return trans->data;
}

static void *
prepare_write_zs_surface(struct d3d12_resource *res,
                         const struct pipe_box *box,
                         struct d3d12_transfer *trans)
{
   prepare_zs_layer_strides(res, box, trans);
   uint32_t *buf = (uint32_t *)malloc(trans->base.layer_stride);
   if (!buf)
      return NULL;

   trans->data = buf;
   return trans->data;
}

static void
write_zs_surface(struct pipe_context *pctx, struct d3d12_resource *res,
                 struct d3d12_transfer *trans)
{
   struct pipe_resource tmpl;
   memset(&tmpl, 0, sizeof tmpl);
   tmpl.target = PIPE_BUFFER;
   tmpl.format = PIPE_FORMAT_R32_UNORM;
   tmpl.bind = 0;
   tmpl.usage = PIPE_USAGE_STAGING;
   tmpl.flags = 0;
   tmpl.width0 = trans->base.layer_stride;
   tmpl.height0 = 1;
   tmpl.depth0 = 1;
   tmpl.array_size = 1;

   local_resource depth_buffer(pctx->screen, &tmpl);
   if (!depth_buffer) {
      debug_printf("Allocating staging buffer for depth failed\n");
      return;
   }

   local_resource stencil_buffer(pctx->screen, &tmpl);
   if (!stencil_buffer) {
      debug_printf("Allocating staging buffer for depth failed\n");
      return;
   }

   void *depth_ptr = depth_buffer.map();
   if (!depth_ptr) {
      debug_printf("Mapping staging depth buffer failed\n");
      return;
   }

   uint8_t *stencil_ptr =  (uint8_t *)stencil_buffer.map();
   if (!stencil_ptr) {
      debug_printf("Mapping staging stencil buffer failed\n");
      return;
   }

   switch (res->base.format) {
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      util_format_z32_unorm_unpack_z_32unorm((uint32_t *)depth_ptr, trans->base.stride, (uint8_t*)trans->data,
                                             trans->base.stride, trans->base.box.width,
                                             trans->base.box.height);
      util_format_z24_unorm_s8_uint_unpack_s_8uint(stencil_ptr, trans->base.stride, (uint8_t*)trans->data,
                                                   trans->base.stride, trans->base.box.width,
                                                   trans->base.box.height);
      break;
   case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
      util_format_z32_float_s8x24_uint_unpack_z_float((float *)depth_ptr, trans->base.stride, (uint8_t*)trans->data,
                                                      trans->base.stride, trans->base.box.width,
                                                      trans->base.box.height);
      util_format_z32_float_s8x24_uint_unpack_s_8uint(stencil_ptr, trans->base.stride, (uint8_t*)trans->data,
                                                      trans->base.stride, trans->base.box.width,
                                                      trans->base.box.height);
      break;
   default:
      unreachable("Unsupported depth steancil format");
   };

   stencil_buffer.unmap();
   depth_buffer.unmap();

   transfer_buf_to_image(d3d12_context(pctx), res, depth_buffer, trans, 0);
   transfer_buf_to_image(d3d12_context(pctx), res, stencil_buffer, trans, 1);
}

static void *
d3d12_transfer_map(struct pipe_context *pctx,
                   struct pipe_resource *pres,
                   unsigned level,
                   unsigned usage,
                   const struct pipe_box *box,
                   struct pipe_transfer **transfer)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_resource *res = d3d12_resource(pres);

   if (usage & PIPE_MAP_DIRECTLY || !res->bo)
      return NULL;

   struct d3d12_transfer *trans = (struct d3d12_transfer *)slab_alloc(&ctx->transfer_pool);
   struct pipe_transfer *ptrans = &trans->base;
   if (!trans)
      return NULL;

   memset(trans, 0, sizeof(*trans));
   pipe_resource_reference(&ptrans->resource, pres);

   ptrans->resource = pres;
   ptrans->level = level;
   ptrans->usage = (enum pipe_map_flags)usage;
   ptrans->box = *box;

   D3D12_RANGE range;
   range.Begin = 0;

   void *ptr;
   if (can_map_directly(&res->base)) {
      if (pres->target == PIPE_BUFFER) {
         ptrans->stride = 0;
         ptrans->layer_stride = 0;
      } else {
         ptrans->stride = util_format_get_stride(pres->format, box->width);
         ptrans->layer_stride = util_format_get_2d_size(pres->format,
                                                        ptrans->stride,
                                                        box->height);
      }

      range = linear_range(box, ptrans->stride, ptrans->layer_stride);
      if (!synchronize(ctx, res, usage, &range))
         return NULL;
      ptr = d3d12_bo_map(res->bo, &range);
   } else if (unlikely(pres->format == PIPE_FORMAT_Z24_UNORM_S8_UINT ||
                       pres->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT)) {
      if (usage & PIPE_MAP_READ) {
         ptr = read_zs_surface(ctx, res, box, trans);
      } else if (usage & PIPE_MAP_WRITE){
         ptr = prepare_write_zs_surface(res, box, trans);
      } else {
         ptr = nullptr;
      }
   } else {
      ptrans->stride = align(util_format_get_stride(pres->format, box->width),
                              D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
      ptrans->layer_stride = util_format_get_2d_size(pres->format,
                                                     ptrans->stride,
                                                     box->height);

      if (res->base.target != PIPE_TEXTURE_3D)
         ptrans->layer_stride = align(ptrans->layer_stride,
                                      D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

      trans->staging_res = pipe_buffer_create(pctx->screen, 0,
                                              PIPE_USAGE_STAGING,
                                              ptrans->layer_stride * box->depth);
      if (!trans->staging_res)
         return NULL;

      struct d3d12_resource *staging_res = d3d12_resource(trans->staging_res);

      if (usage & PIPE_MAP_READ) {
         bool ret = transfer_image_to_buf(ctx, res, staging_res, trans, 0);
         if (ret == false)
            return NULL;
         d3d12_flush_cmdlist_and_wait(ctx);
      }

      range.Begin = 0;
      range.End = ptrans->layer_stride * box->depth;

      ptr = d3d12_bo_map(staging_res->bo, &range);
   }

   *transfer = ptrans;
   return ptr;
}

static void
d3d12_transfer_unmap(struct pipe_context *pctx,
                     struct pipe_transfer *ptrans)
{
   struct d3d12_resource *res = d3d12_resource(ptrans->resource);
   struct d3d12_transfer *trans = (struct d3d12_transfer *)ptrans;
   D3D12_RANGE range = { 0, 0 };

   if (trans->data != nullptr) {
      if (trans->base.usage & PIPE_MAP_WRITE)
         write_zs_surface(pctx, res, trans);
      free(trans->data);
   } else if (trans->staging_res) {
      struct d3d12_resource *staging_res = d3d12_resource(trans->staging_res);

      if (trans->base.usage & PIPE_MAP_WRITE) {
         range.Begin = 0;
         range.End = ptrans->layer_stride * ptrans->box.depth;
      }
      d3d12_bo_unmap(staging_res->bo, &range);

      if (trans->base.usage & PIPE_MAP_WRITE) {
         struct d3d12_context *ctx = d3d12_context(pctx);
         transfer_buf_to_image(ctx, res, staging_res, trans, 0);
      }

      pipe_resource_reference(&trans->staging_res, NULL);
   } else {
      if (trans->base.usage & PIPE_MAP_WRITE) {
         range.Begin = ptrans->box.x;
         range.End = ptrans->box.x + ptrans->box.width;
      }
      d3d12_bo_unmap(res->bo, &range);
   }

   pipe_resource_reference(&ptrans->resource, NULL);
   slab_free(&d3d12_context(pctx)->transfer_pool, ptrans);
}

void
d3d12_resource_make_writeable(struct pipe_context *pctx,
                              struct pipe_resource *pres)
{
   struct d3d12_context *ctx = d3d12_context(pctx);
   struct d3d12_resource *res = d3d12_resource(pres);
   struct d3d12_resource *dup_res;

   if (!res->bo || !d3d12_bo_is_suballocated(res->bo))
      return;

   dup_res = d3d12_resource(pipe_buffer_create(pres->screen,
                                               pres->bind & PIPE_BIND_STREAM_OUTPUT,
                                               (pipe_resource_usage) pres->usage,
                                               pres->width0));

   if (res->valid_buffer_range.end > res->valid_buffer_range.start) {
      struct pipe_box box;

      box.x = res->valid_buffer_range.start;
      box.y = 0;
      box.z = 0;
      box.width = res->valid_buffer_range.end - res->valid_buffer_range.start;
      box.height = 1;
      box.depth = 1;

      d3d12_direct_copy(ctx, dup_res, 0, &box, res, 0, &box, PIPE_MASK_RGBAZS);
   }

   /* Move new BO to old resource */
   d3d12_bo_unreference(res->bo);
   res->bo = dup_res->bo;
   d3d12_bo_reference(res->bo);

   d3d12_resource_destroy(dup_res->base.screen, &dup_res->base);
}

void
d3d12_context_resource_init(struct pipe_context *pctx)
{
   pctx->transfer_map = d3d12_transfer_map;
   pctx->transfer_unmap = d3d12_transfer_unmap;

   pctx->transfer_flush_region = u_default_transfer_flush_region;
   pctx->buffer_subdata = u_default_buffer_subdata;
   pctx->texture_subdata = u_default_texture_subdata;
}
