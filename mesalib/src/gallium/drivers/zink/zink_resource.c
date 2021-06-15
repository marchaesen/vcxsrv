/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "zink_resource.h"

#include "zink_batch.h"
#include "zink_context.h"
#include "zink_fence.h"
#include "zink_program.h"
#include "zink_screen.h"

#include "vulkan/wsi/wsi_common.h"

#include "util/slab.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/u_transfer_helper.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"

#include "frontend/sw_winsys.h"

#ifndef _WIN32
#define ZINK_USE_DMABUF
#endif

#ifdef ZINK_USE_DMABUF
#include "drm-uapi/drm_fourcc.h"
#endif

static void
zink_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box);
static void *
zink_transfer_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer);
static void
zink_transfer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *ptrans);

void
debug_describe_zink_resource_object(char *buf, const struct zink_resource_object *ptr)
{
   sprintf(buf, "zink_resource_object");
}

static uint32_t
get_resource_usage(struct zink_resource *res)
{
   uint32_t reads = p_atomic_read(&res->obj->reads.usage);
   uint32_t writes = p_atomic_read(&res->obj->writes.usage);
   uint32_t batch_uses = 0;
   if (reads)
      batch_uses |= ZINK_RESOURCE_ACCESS_READ;
   if (writes)
      batch_uses |= ZINK_RESOURCE_ACCESS_WRITE;
   return batch_uses;
}

static void
resource_sync_reads(struct zink_context *ctx, struct zink_resource *res)
{
   uint32_t reads = p_atomic_read(&res->obj->reads.usage);
   assert(reads);
   zink_wait_on_batch(ctx, reads);
}

static void
resource_sync_writes_from_batch_usage(struct zink_context *ctx, struct zink_resource *res)
{
   uint32_t writes = p_atomic_read(&res->obj->writes.usage);

   zink_wait_on_batch(ctx, writes);
}

static uint32_t
mem_hash(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct mem_key));
}

static bool
mem_equals(const void *a, const void *b)
{
   return !memcmp(a, b, sizeof(struct mem_key));
}

static void
cache_or_free_mem(struct zink_screen *screen, struct zink_resource_object *obj)
{
   if (obj->mkey.flags) {
      simple_mtx_lock(&screen->mem_cache_mtx);
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(screen->resource_mem_cache, obj->mem_hash, &obj->mkey);
      struct util_dynarray *array = he ? (void*)he->data : NULL;
      if (!array) {
         struct mem_key *mkey = rzalloc(screen->resource_mem_cache, struct mem_key);
         memcpy(mkey, &obj->mkey, sizeof(struct mem_key));
         array = rzalloc(screen->resource_mem_cache, struct util_dynarray);
         util_dynarray_init(array, screen->resource_mem_cache);
         _mesa_hash_table_insert_pre_hashed(screen->resource_mem_cache, obj->mem_hash, mkey, array);
      }
      if (util_dynarray_num_elements(array, struct mem_cache_entry) < 5) {
         struct mem_cache_entry mc = { obj->mem, obj->map };
         util_dynarray_append(array, struct mem_cache_entry, mc);
         simple_mtx_unlock(&screen->mem_cache_mtx);
         return;
      }
      simple_mtx_unlock(&screen->mem_cache_mtx);
   }
   vkFreeMemory(screen->dev, obj->mem, NULL);
}

void
zink_destroy_resource_object(struct zink_screen *screen, struct zink_resource_object *obj)
{
   if (obj->is_buffer) {
      if (obj->sbuffer)
         vkDestroyBuffer(screen->dev, obj->sbuffer, NULL);
      vkDestroyBuffer(screen->dev, obj->buffer, NULL);
   } else {
      vkDestroyImage(screen->dev, obj->image, NULL);
   }

   zink_descriptor_set_refs_clear(&obj->desc_set_refs, obj);
   cache_or_free_mem(screen, obj);
   FREE(obj);
}

static void
zink_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pres)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   if (pres->target == PIPE_BUFFER)
      util_range_destroy(&res->valid_buffer_range);

   zink_resource_object_reference(screen, &res->obj, NULL);
   zink_resource_object_reference(screen, &res->scanout_obj, NULL);
   threaded_resource_deinit(pres);
   FREE(res);
}

static uint32_t
get_memory_type_index(struct zink_screen *screen,
                      const VkMemoryRequirements *reqs,
                      VkMemoryPropertyFlags props)
{
   int32_t idx = -1;
   for (uint32_t i = 0u; i < VK_MAX_MEMORY_TYPES; i++) {
      if (((reqs->memoryTypeBits >> i) & 1) == 1) {
         if ((screen->info.mem_props.memoryTypes[i].propertyFlags & props) == props) {
            if (!(props & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
                screen->info.mem_props.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
               idx = i;
            } else
               return i;
         }
      }
   }
   if (idx >= 0)
      return idx;

   if (props & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      /* if no suitable cached memory can be found, fall back
       * to non-cached memory instead.
       */
      return get_memory_type_index(screen, reqs,
         props & ~VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
   }

   unreachable("Unsupported memory-type");
   return 0;
}

static VkImageAspectFlags
aspect_from_format(enum pipe_format fmt)
{
   if (util_format_is_depth_or_stencil(fmt)) {
      VkImageAspectFlags aspect = 0;
      const struct util_format_description *desc = util_format_description(fmt);
      if (util_format_has_depth(desc))
         aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
      if (util_format_has_stencil(desc))
         aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
      return aspect;
   } else
     return VK_IMAGE_ASPECT_COLOR_BIT;
}

static VkBufferCreateInfo
create_bci(struct zink_screen *screen, const struct pipe_resource *templ, unsigned bind)
{
   VkBufferCreateInfo bci = {};
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.size = templ->width0;
   assert(bci.size > 0);

   bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

   VkFormatProperties props = screen->format_props[templ->format];

   bci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT |
                VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;
   if (props.bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)
      bci.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
   if (props.bufferFeatures & VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT)
      bci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT;

   if (bind & PIPE_BIND_SHADER_IMAGE) {
      assert(props.bufferFeatures & VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT);
      bci.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
      bci.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
   return bci;
}

static VkImageUsageFlags
get_image_usage(struct zink_screen *screen, VkImageTiling tiling, const struct pipe_resource *templ, unsigned bind)
{
   VkFormatProperties props = screen->format_props[templ->format];
   VkFormatFeatureFlags feats = tiling == VK_IMAGE_TILING_LINEAR ? props.linearTilingFeatures : props.optimalTilingFeatures;
   VkImageUsageFlags usage = 0;
   /* sadly, gallium doesn't let us know if it'll ever need this, so we have to assume */
   if (feats & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)
      usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
   if (feats & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
      usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   if (feats & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT && !((bind & (PIPE_BIND_LINEAR | PIPE_BIND_SCANOUT)) == (PIPE_BIND_LINEAR | PIPE_BIND_SCANOUT)))
      usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

   if ((templ->nr_samples <= 1 || screen->info.feats.features.shaderStorageImageMultisample) &&
       (bind & PIPE_BIND_SHADER_IMAGE)) {
      if ((tiling == VK_IMAGE_TILING_LINEAR && props.linearTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
          (tiling == VK_IMAGE_TILING_OPTIMAL && props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))
         usage |= VK_IMAGE_USAGE_STORAGE_BIT;
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (feats & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
         usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      else
         return 0;
   }

   if (bind & PIPE_BIND_DEPTH_STENCIL) {
      if (feats & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)
         usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
      else
         return 0;
   /* this is unlikely to occur and has been included for completeness */
   } else if (bind & PIPE_BIND_SAMPLER_VIEW && !(usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
      if (feats & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)
         usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
      else
         return 0;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
      usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;

   if (bind & PIPE_BIND_STREAM_OUTPUT)
      usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
   return usage;
}

static VkImageCreateInfo
create_ici(struct zink_screen *screen, const struct pipe_resource *templ, unsigned bind)
{
   VkImageCreateInfo ici = {};
   ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   ici.flags = bind & (PIPE_BIND_SCANOUT | PIPE_BIND_DEPTH_STENCIL) ? 0 : VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;

   switch (templ->target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      ici.imageType = VK_IMAGE_TYPE_1D;
      break;

   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      ici.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_RECT:
      ici.imageType = VK_IMAGE_TYPE_2D;
      break;

   case PIPE_TEXTURE_3D:
      ici.imageType = VK_IMAGE_TYPE_3D;
      if (bind & PIPE_BIND_RENDER_TARGET)
         ici.flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
      break;

   case PIPE_BUFFER:
      unreachable("PIPE_BUFFER should already be handled");

   default:
      unreachable("Unknown target");
   }

   ici.format = zink_get_format(screen, templ->format);
   ici.extent.width = templ->width0;
   ici.extent.height = templ->height0;
   ici.extent.depth = templ->depth0;
   ici.mipLevels = templ->last_level + 1;
   ici.arrayLayers = MAX2(templ->array_size, 1);
   ici.samples = templ->nr_samples ? templ->nr_samples : VK_SAMPLE_COUNT_1_BIT;
   ici.tiling = bind & PIPE_BIND_LINEAR ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;

   if (templ->target == PIPE_TEXTURE_CUBE ||
       templ->target == PIPE_TEXTURE_CUBE_ARRAY)
      ici.arrayLayers *= 6;

   if (templ->usage == PIPE_USAGE_STAGING)
      ici.tiling = VK_IMAGE_TILING_LINEAR;

   ici.usage = get_image_usage(screen, ici.tiling, templ, bind);
   if (!ici.usage) {
      assert(ici.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
      ici.tiling = !ici.tiling;
      ici.usage = get_image_usage(screen, ici.tiling, templ, bind);
   }

   ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
   return ici;
}

static struct zink_resource_object *
resource_object_create(struct zink_screen *screen, const struct pipe_resource *templ, struct winsys_handle *whandle, bool *optimal_tiling)
{
   struct zink_resource_object *obj = CALLOC_STRUCT(zink_resource_object);
   if (!obj)
      return NULL;

   VkMemoryRequirements reqs = {};
   VkMemoryPropertyFlags flags;
   bool scanout = templ->bind & PIPE_BIND_SCANOUT;
   bool shared = templ->bind & PIPE_BIND_SHARED;

   pipe_reference_init(&obj->reference, 1);
   util_dynarray_init(&obj->desc_set_refs.refs, NULL);
   if (templ->target == PIPE_BUFFER) {
      VkBufferCreateInfo bci = create_bci(screen, templ, templ->bind);

      if (vkCreateBuffer(screen->dev, &bci, NULL, &obj->buffer) != VK_SUCCESS) {
         debug_printf("vkCreateBuffer failed\n");
         goto fail1;
      }

      vkGetBufferMemoryRequirements(screen->dev, obj->buffer, &reqs);
      if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
         flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      else
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      obj->is_buffer = true;
   } else {
      VkImageCreateInfo ici = create_ici(screen, templ, templ->bind);
      VkExternalMemoryImageCreateInfo emici = {};

      if (templ->bind & PIPE_BIND_SHARED) {
         emici.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
         emici.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
         ici.pNext = &emici;

         if (ici.tiling == VK_IMAGE_TILING_OPTIMAL) {
            // TODO: remove for wsi
            ici.pNext = NULL;
            scanout = false;
            shared = false;
         }

      }

      if (optimal_tiling)
         *optimal_tiling = ici.tiling != VK_IMAGE_TILING_LINEAR;

      VkImageFormatProperties image_props;
      VkResult ret;
      if (screen->vk_GetPhysicalDeviceImageFormatProperties2) {
         VkImageFormatProperties2 props2 = {};
         props2.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
         VkPhysicalDeviceImageFormatInfo2 info = {};
         info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
         info.format = ici.format;
         info.type = ici.imageType;
         info.tiling = ici.tiling;
         info.usage = ici.usage;
         info.flags = ici.flags;
         ret = screen->vk_GetPhysicalDeviceImageFormatProperties2(screen->pdev, &info, &props2);
         image_props = props2.imageFormatProperties;
      } else
         ret = vkGetPhysicalDeviceImageFormatProperties(screen->pdev, ici.format, ici.imageType,
                                                      ici.tiling, ici.usage, ici.flags, &image_props);
      if (ret != VK_SUCCESS) {
         FREE(obj);
         return NULL;
      }

      struct wsi_image_create_info image_wsi_info = {
         VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
         NULL,
         .scanout = true,
      };

      if ((screen->needs_mesa_wsi || screen->needs_mesa_flush_wsi) && scanout) {
         image_wsi_info.pNext = ici.pNext;
         ici.pNext = &image_wsi_info;
      }

      VkResult result = vkCreateImage(screen->dev, &ici, NULL, &obj->image);
      if (result != VK_SUCCESS) {
         debug_printf("vkCreateImage failed\n");
         goto fail1;
      }

      vkGetImageMemoryRequirements(screen->dev, obj->image, &reqs);
      if (templ->usage == PIPE_USAGE_STAGING && ici.tiling == VK_IMAGE_TILING_LINEAR)
        flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      else
        flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
   }

   if (templ->flags & PIPE_RESOURCE_FLAG_MAP_COHERENT || templ->usage == PIPE_USAGE_DYNAMIC)
      flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   else if (!(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            templ->usage == PIPE_USAGE_STAGING)
      flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

   VkMemoryAllocateInfo mai = {};
   mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.allocationSize = reqs.size;
   mai.memoryTypeIndex = get_memory_type_index(screen, &reqs, flags);

   obj->coherent = flags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

   if (templ->target != PIPE_BUFFER) {
      VkMemoryType mem_type =
         screen->info.mem_props.memoryTypes[mai.memoryTypeIndex];
      obj->host_visible = mem_type.propertyFlags &
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
   } else if (!(templ->flags & PIPE_RESOURCE_FLAG_SPARSE)) {
      obj->host_visible = true;
      if (!obj->coherent)
         mai.allocationSize = reqs.size = align(reqs.size, screen->info.props.limits.nonCoherentAtomSize);
   }

   VkExportMemoryAllocateInfo emai = {};
   if (templ->bind & PIPE_BIND_SHARED && shared) {
      emai.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
      emai.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

      emai.pNext = mai.pNext;
      mai.pNext = &emai;
   }

   VkImportMemoryFdInfoKHR imfi = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      NULL,
   };

   if (whandle && whandle->type == WINSYS_HANDLE_TYPE_FD) {
      imfi.pNext = NULL;
      imfi.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      imfi.fd = whandle->handle;

      imfi.pNext = mai.pNext;
      emai.pNext = &imfi;
   }

   struct wsi_memory_allocate_info memory_wsi_info = {
      VK_STRUCTURE_TYPE_WSI_MEMORY_ALLOCATE_INFO_MESA,
      NULL,
   };

   if (screen->needs_mesa_wsi && scanout) {
      memory_wsi_info.implicit_sync = true;

      memory_wsi_info.pNext = mai.pNext;
      mai.pNext = &memory_wsi_info;
   }

   if (!mai.pNext && !(templ->flags & (PIPE_RESOURCE_FLAG_MAP_COHERENT | PIPE_RESOURCE_FLAG_SPARSE))) {
      obj->mkey.reqs = reqs;
      obj->mkey.flags = flags;
      obj->mem_hash = mem_hash(&obj->mkey);
      simple_mtx_lock(&screen->mem_cache_mtx);

      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(screen->resource_mem_cache, obj->mem_hash, &obj->mkey);

      struct util_dynarray *array = he ? (void*)he->data : NULL;
      if (array && util_dynarray_num_elements(array, struct mem_cache_entry)) {
         struct mem_cache_entry mc = util_dynarray_pop(array, struct mem_cache_entry);
         obj->mem = mc.mem;
         obj->map = mc.map;
      }
      simple_mtx_unlock(&screen->mem_cache_mtx);
   }

   /* TODO: sparse buffers should probably allocate multiple regions of memory instead of giant blobs? */
   if (!obj->mem && vkAllocateMemory(screen->dev, &mai, NULL, &obj->mem) != VK_SUCCESS) {
      debug_printf("vkAllocateMemory failed\n");
      goto fail2;
   }

   obj->offset = 0;
   obj->size = reqs.size;

   if (templ->target == PIPE_BUFFER) {
      if (!(templ->flags & PIPE_RESOURCE_FLAG_SPARSE))
         vkBindBufferMemory(screen->dev, obj->buffer, obj->mem, obj->offset);
   } else
      vkBindImageMemory(screen->dev, obj->image, obj->mem, obj->offset);
   return obj;

fail2:
   if (templ->target == PIPE_BUFFER)
      vkDestroyBuffer(screen->dev, obj->buffer, NULL);
   else
      vkDestroyImage(screen->dev, obj->image, NULL);
fail1:
   FREE(obj);
   return NULL;
}

static const struct u_resource_vtbl zink_resource_vtbl = {
   NULL,
   zink_resource_destroy,
   zink_transfer_map,
   zink_transfer_flush_region,
   zink_transfer_unmap,
};

static struct pipe_resource *
resource_create(struct pipe_screen *pscreen,
                const struct pipe_resource *templ,
                struct winsys_handle *whandle,
                unsigned external_usage)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = CALLOC_STRUCT(zink_resource);

   res->base.b = *templ;

   res->base.vtbl = &zink_resource_vtbl;
   threaded_resource_init(&res->base.b);
   pipe_reference_init(&res->base.b.reference, 1);
   res->base.b.screen = pscreen;

   bool optimal_tiling = false;
   res->obj = resource_object_create(screen, templ, whandle, &optimal_tiling);
   if (!res->obj) {
      FREE(res);
      return NULL;
   }

   res->internal_format = templ->format;
   if (templ->target == PIPE_BUFFER) {
      util_range_init(&res->valid_buffer_range);
   } else {
      res->format = zink_get_format(screen, templ->format);
      res->layout = VK_IMAGE_LAYOUT_UNDEFINED;
      res->optimal_tiling = optimal_tiling;
      res->aspect = aspect_from_format(templ->format);
      if (res->base.b.bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED) && optimal_tiling) {
         // TODO: remove for wsi
         struct pipe_resource templ2 = res->base.b;
         templ2.bind = (res->base.b.bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)) | PIPE_BIND_LINEAR;
         res->scanout_obj = resource_object_create(screen, &templ2, whandle, &optimal_tiling);
         assert(!optimal_tiling);
      }
   }

   if (screen->winsys && (templ->bind & PIPE_BIND_DISPLAY_TARGET)) {
      struct sw_winsys *winsys = screen->winsys;
      res->dt = winsys->displaytarget_create(screen->winsys,
                                             res->base.b.bind,
                                             res->base.b.format,
                                             templ->width0,
                                             templ->height0,
                                             64, NULL,
                                             &res->dt_stride);
   }

   return &res->base.b;
}

static struct pipe_resource *
zink_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templ)
{
   return resource_create(pscreen, templ, NULL, 0);
}

static bool
zink_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *context,
                         struct pipe_resource *tex,
                         struct winsys_handle *whandle,
                         unsigned usage)
{
   struct zink_resource *res = zink_resource(tex);
   struct zink_screen *screen = zink_screen(pscreen);
   //TODO: remove for wsi
   struct zink_resource_object *obj = res->scanout_obj ? res->scanout_obj : res->obj;

   if (res->base.b.target != PIPE_BUFFER) {
      VkImageSubresource sub_res = {};
      VkSubresourceLayout sub_res_layout = {};

      sub_res.aspectMask = res->aspect;

      vkGetImageSubresourceLayout(screen->dev, obj->image, &sub_res, &sub_res_layout);

      whandle->stride = sub_res_layout.rowPitch;
   }

   if (whandle->type == WINSYS_HANDLE_TYPE_FD) {
#ifdef ZINK_USE_DMABUF
      VkMemoryGetFdInfoKHR fd_info = {};
      int fd;
      fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
      //TODO: remove for wsi
      fd_info.memory = obj->mem;
      fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      VkResult result = (*screen->vk_GetMemoryFdKHR)(screen->dev, &fd_info, &fd);
      if (result != VK_SUCCESS)
         return false;
      whandle->handle = fd;
      whandle->modifier = DRM_FORMAT_MOD_INVALID;
#else
      return false;
#endif
   }
   return true;
}

static struct pipe_resource *
zink_resource_from_handle(struct pipe_screen *pscreen,
                 const struct pipe_resource *templ,
                 struct winsys_handle *whandle,
                 unsigned usage)
{
#ifdef ZINK_USE_DMABUF
   if (whandle->modifier != DRM_FORMAT_MOD_INVALID)
      return NULL;

   return resource_create(pscreen, templ, whandle, usage);
#else
   return NULL;
#endif
}

static bool
invalidate_buffer(struct zink_context *ctx, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   assert(res->base.b.target == PIPE_BUFFER);

   if (res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE)
      return false;

   if (res->valid_buffer_range.start > res->valid_buffer_range.end)
      return false;

   if (res->bind_history & ZINK_RESOURCE_USAGE_STREAMOUT)
      ctx->dirty_so_targets = true;
   /* force counter buffer reset */
   res->bind_history &= ~ZINK_RESOURCE_USAGE_STREAMOUT;

   util_range_set_empty(&res->valid_buffer_range);
   if (!get_resource_usage(res))
      return false;

   struct zink_resource_object *old_obj = res->obj;
   struct zink_resource_object *new_obj = resource_object_create(screen, &res->base.b, NULL, NULL);
   if (!new_obj) {
      debug_printf("new backing resource alloc failed!");
      return false;
   }
   res->obj = new_obj;
   res->access_stage = 0;
   res->access = 0;
   zink_resource_rebind(ctx, res);
   zink_descriptor_set_refs_clear(&old_obj->desc_set_refs, old_obj);
   zink_resource_object_reference(screen, &old_obj, NULL);
   return true;
}


static void
zink_resource_invalidate(struct pipe_context *pctx, struct pipe_resource *pres)
{
   if (pres->target == PIPE_BUFFER)
      invalidate_buffer(zink_context(pctx), zink_resource(pres));
}

static void
zink_transfer_copy_bufimage(struct zink_context *ctx,
                            struct zink_resource *dst,
                            struct zink_resource *src,
                            struct zink_transfer *trans)
{
   assert((trans->base.b.usage & (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY)) !=
          (PIPE_MAP_DEPTH_ONLY | PIPE_MAP_STENCIL_ONLY));

   bool buf2img = src->base.b.target == PIPE_BUFFER;

   struct pipe_box box = trans->base.b.box;
   int x = box.x;
   if (buf2img)
      box.x = src->obj->offset + trans->offset;

   zink_copy_image_buffer(ctx, NULL, dst, src, trans->base.b.level, buf2img ? x : dst->obj->offset,
                           box.y, box.z, trans->base.b.level, &box, trans->base.b.usage);
}

bool
zink_resource_has_usage(struct zink_resource *res, enum zink_resource_access usage)
{
   uint32_t batch_uses = get_resource_usage(res);
   return batch_uses & usage;
}

static VkMappedMemoryRange
init_mem_range(struct zink_screen *screen, struct zink_resource *res, VkDeviceSize offset, VkDeviceSize size)
{
   assert(res->obj->size);
   VkDeviceSize align = offset % screen->info.props.limits.nonCoherentAtomSize;
   if (screen->info.props.limits.nonCoherentAtomSize - 1 > offset)
      offset = 0;
   else
      offset -= align, size += align;
   align = screen->info.props.limits.nonCoherentAtomSize - (size % screen->info.props.limits.nonCoherentAtomSize);
   if (offset + size + align > res->obj->size)
      size = res->obj->size - offset;
   else
      size += align;
   VkMappedMemoryRange range = {
      VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      NULL,
      res->obj->mem,
      offset,
      size
   };
   assert(range.size);
   return range;
}

bool
zink_resource_has_curr_read_usage(struct zink_context *ctx, struct zink_resource *res)
{
   return zink_batch_usage_matches(&res->obj->reads, ctx->curr_batch);
}

static uint32_t
get_most_recent_access(struct zink_resource *res, enum zink_resource_access flags)
{
   uint32_t usage[3]; // read, write, failure
   uint32_t latest = ARRAY_SIZE(usage) - 1;
   usage[latest] = 0;

   if (flags & ZINK_RESOURCE_ACCESS_READ) {
      usage[0] = p_atomic_read(&res->obj->reads.usage);
      if (usage[0] > usage[latest]) {
         latest = 0;
      }
   }
   if (flags & ZINK_RESOURCE_ACCESS_WRITE) {
      usage[1] = p_atomic_read(&res->obj->writes.usage);
      if (usage[1] > usage[latest]) {
         latest = 1;
      }
   }
   return usage[latest];
}

static void *
map_resource(struct zink_screen *screen, struct zink_resource *res)
{
   VkResult result = VK_SUCCESS;
   if (res->obj->map)
      return res->obj->map;
   assert(!(res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE));
   result = vkMapMemory(screen->dev, res->obj->mem, res->obj->offset,
                        res->obj->size, 0, &res->obj->map);
   return result == VK_SUCCESS ? res->obj->map : NULL;
}

static void
unmap_resource(struct zink_screen *screen, struct zink_resource *res)
{
   res->obj->map = NULL;
   vkUnmapMemory(screen->dev, res->obj->mem);
}

static void *
buffer_transfer_map(struct zink_context *ctx, struct zink_resource *res, unsigned usage,
                    const struct pipe_box *box, struct zink_transfer *trans)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   void *ptr = NULL;

   if (res->base.is_user_ptr)
      usage |= PIPE_MAP_PERSISTENT;

   /* See if the buffer range being mapped has never been initialized,
    * in which case it can be mapped unsynchronized. */
   if (!(usage & (PIPE_MAP_UNSYNCHRONIZED | TC_TRANSFER_MAP_NO_INFER_UNSYNCHRONIZED)) &&
       usage & PIPE_MAP_WRITE && !res->base.is_shared &&
       !util_ranges_intersect(&res->valid_buffer_range, box->x, box->x + box->width)) {
      usage |= PIPE_MAP_UNSYNCHRONIZED;
   }

   /* If discarding the entire range, discard the whole resource instead. */
   if (usage & PIPE_MAP_DISCARD_RANGE && box->x == 0 && box->width == res->base.b.width0) {
      usage |= PIPE_MAP_DISCARD_WHOLE_RESOURCE;
   }

   if (usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE &&
       !(usage & (PIPE_MAP_UNSYNCHRONIZED | TC_TRANSFER_MAP_NO_INVALIDATE))) {
      assert(usage & PIPE_MAP_WRITE);

      if (invalidate_buffer(ctx, res)) {
         /* At this point, the buffer is always idle. */
         usage |= PIPE_MAP_UNSYNCHRONIZED;
      } else {
         /* Fall back to a temporary buffer. */
         usage |= PIPE_MAP_DISCARD_RANGE;
      }
   }

   if ((usage & PIPE_MAP_WRITE) &&
       (usage & PIPE_MAP_DISCARD_RANGE || (!(usage & PIPE_MAP_READ) && zink_resource_has_usage(res, ZINK_RESOURCE_ACCESS_RW))) &&
       ((res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE) || !(usage & (PIPE_MAP_UNSYNCHRONIZED | PIPE_MAP_PERSISTENT)))) {

      /* Check if mapping this buffer would cause waiting for the GPU.
       */

      uint32_t latest_access = get_most_recent_access(res, ZINK_RESOURCE_ACCESS_RW);
      if (res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE ||
          zink_resource_has_curr_read_usage(ctx, res) ||
          (latest_access && !zink_check_batch_completion(ctx, latest_access))) {
         /* Do a wait-free write-only transfer using a temporary buffer. */
         unsigned offset;

         /* If we are not called from the driver thread, we have
          * to use the uploader from u_threaded_context, which is
          * local to the calling thread.
          */
         struct u_upload_mgr *mgr;
         if (usage & TC_TRANSFER_MAP_THREADED_UNSYNC)
            mgr = ctx->tc->base.stream_uploader;
         else
            mgr = ctx->base.stream_uploader;
         u_upload_alloc(mgr, 0, box->width + box->x,
                     screen->info.props.limits.minMemoryMapAlignment, &offset,
                     (struct pipe_resource **)&trans->staging_res, (void **)&ptr);
         res = zink_resource(trans->staging_res);
         trans->offset = offset;
         res->obj->map = ptr;
      } else {
         /* At this point, the buffer is always idle (we checked it above). */
         usage |= PIPE_MAP_UNSYNCHRONIZED;
      }
   } else if ((usage & PIPE_MAP_READ) && !(usage & PIPE_MAP_PERSISTENT)) {
      assert(!(usage & (TC_TRANSFER_MAP_THREADED_UNSYNC | PIPE_MAP_THREAD_SAFE)));
      uint32_t latest_write = get_most_recent_access(res, ZINK_RESOURCE_ACCESS_WRITE);
      if (usage & PIPE_MAP_DONTBLOCK) {
         /* sparse will always need to wait since it has to copy */
         if (res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE)
            return NULL;
         if (latest_write &&
             (latest_write == ctx->curr_batch || !zink_check_batch_completion(ctx, latest_write)))
            return NULL;
         latest_write = 0;
      }
      if (res->base.b.flags & PIPE_RESOURCE_FLAG_SPARSE) {
         zink_fence_wait(&ctx->base);
         trans->staging_res = pipe_buffer_create(&screen->base, PIPE_BIND_LINEAR, PIPE_USAGE_STAGING, box->x + box->width);
         if (!trans->staging_res)
            return NULL;
         struct zink_resource *staging_res = zink_resource(trans->staging_res);
         trans->offset = staging_res->obj->offset;
         zink_copy_buffer(ctx, NULL, staging_res, res, box->x, box->x, box->width);
         res = staging_res;
         latest_write = ctx->curr_batch;
      }
      if (latest_write)
         zink_wait_on_batch(ctx, latest_write);
   }

   if (!ptr) {
      ptr = map_resource(screen, res);
      if (!ptr)
         return NULL;
   }

   if (!res->obj->coherent
#if defined(MVK_VERSION)
      // Work around for MoltenVk limitation specifically on coherent memory
      // MoltenVk returns blank memory ranges when there should be data present
      // This is a known limitation of MoltenVK.
      // See https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md#known-moltenvk-limitations

       || screen->have_moltenvk
#endif
      ) {
      VkDeviceSize size = box->width;
      VkDeviceSize offset = trans->offset + box->x;
      VkMappedMemoryRange range = init_mem_range(screen, res, offset, size);
      if (vkInvalidateMappedMemoryRanges(screen->dev, 1, &range) != VK_SUCCESS) {
         vkUnmapMemory(screen->dev, res->obj->mem);
         return NULL;
      }
   }
   trans->base.b.usage = usage;
   if (usage & PIPE_MAP_WRITE)
      util_range_add(&res->base.b, &res->valid_buffer_range, box->x, box->x + box->width);
   return ptr;
}

static void *
zink_transfer_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);

   struct zink_transfer *trans;

   if (usage & PIPE_MAP_THREAD_SAFE)
      trans = malloc(sizeof(*trans));
   else if (usage & TC_TRANSFER_MAP_THREADED_UNSYNC)
      trans = slab_alloc(&ctx->transfer_pool_unsync);
   else
      trans = slab_alloc(&ctx->transfer_pool);
   if (!trans)
      return NULL;

   memset(trans, 0, sizeof(*trans));
   pipe_resource_reference(&trans->base.b.resource, pres);

   trans->base.b.resource = pres;
   trans->base.b.level = level;
   trans->base.b.usage = usage;
   trans->base.b.box = *box;

   void *ptr, *base;
   if (pres->target == PIPE_BUFFER) {
      base = buffer_transfer_map(ctx, res, usage, box, trans);
      ptr = ((uint8_t *)base) + box->x;
   } else {
      if (usage & PIPE_MAP_WRITE && !(usage & PIPE_MAP_READ))
         /* this is like a blit, so we can potentially dump some clears or maybe we have to  */
         zink_fb_clears_apply_or_discard(ctx, pres, zink_rect_from_box(box), false);
      else if (usage & PIPE_MAP_READ)
         /* if the map region intersects with any clears then we have to apply them */
         zink_fb_clears_apply_region(ctx, pres, zink_rect_from_box(box));
      if (res->optimal_tiling || !res->obj->host_visible) {
         enum pipe_format format = pres->format;
         if (usage & PIPE_MAP_DEPTH_ONLY)
            format = util_format_get_depth_only(pres->format);
         else if (usage & PIPE_MAP_STENCIL_ONLY)
            format = PIPE_FORMAT_S8_UINT;
         trans->base.b.stride = util_format_get_stride(format, box->width);
         trans->base.b.layer_stride = util_format_get_2d_size(format,
                                                            trans->base.b.stride,
                                                            box->height);

         struct pipe_resource templ = *pres;
         templ.format = format;
         templ.usage = usage & PIPE_MAP_READ ? PIPE_USAGE_STAGING : PIPE_USAGE_STREAM;
         templ.target = PIPE_BUFFER;
         templ.bind = PIPE_BIND_LINEAR;
         templ.width0 = trans->base.b.layer_stride * box->depth;
         templ.height0 = templ.depth0 = 0;
         templ.last_level = 0;
         templ.array_size = 1;
         templ.flags = 0;

         trans->staging_res = zink_resource_create(pctx->screen, &templ);
         if (!trans->staging_res)
            return NULL;

         struct zink_resource *staging_res = zink_resource(trans->staging_res);

         if (usage & PIPE_MAP_READ) {
            zink_transfer_copy_bufimage(ctx, staging_res, res, trans);
            /* need to wait for rendering to finish */
            zink_fence_wait(pctx);
         }

         ptr = base = map_resource(screen, staging_res);
         if (!base)
            return NULL;
      } else {
         assert(!res->optimal_tiling);
         base = map_resource(screen, res);
         if (!base)
            return NULL;
         /* special case compute reads since they aren't handled by zink_fence_wait() */
         if (zink_resource_has_usage(res, ZINK_RESOURCE_ACCESS_READ))
            resource_sync_reads(ctx, res);
         if (zink_resource_has_usage(res, ZINK_RESOURCE_ACCESS_RW)) {
            if (usage & PIPE_MAP_READ)
               resource_sync_writes_from_batch_usage(ctx, res);
            else
               zink_fence_wait(pctx);
         }
         VkImageSubresource isr = {
            res->aspect,
            level,
            0
         };
         VkSubresourceLayout srl;
         vkGetImageSubresourceLayout(screen->dev, res->obj->image, &isr, &srl);
         trans->base.b.stride = srl.rowPitch;
         if (res->base.b.target == PIPE_TEXTURE_3D)
            trans->base.b.layer_stride = srl.depthPitch;
         else
            trans->base.b.layer_stride = srl.arrayPitch;
         trans->offset = srl.offset;
         trans->depthPitch = srl.depthPitch;
         const struct util_format_description *desc = util_format_description(res->base.b.format);
         unsigned offset = srl.offset +
                           box->z * srl.depthPitch +
                           (box->y / desc->block.height) * srl.rowPitch +
                           (box->x / desc->block.width) * (desc->block.bits / 8);
         if (!res->obj->coherent) {
            VkDeviceSize size = box->width * box->height * desc->block.bits / 8;
            VkMappedMemoryRange range = init_mem_range(screen, res, offset, size);
            vkFlushMappedMemoryRanges(screen->dev, 1, &range);
         }
         ptr = ((uint8_t *)base) + offset;
      }
   }
   if ((usage & PIPE_MAP_PERSISTENT) && !(usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps++;

   *transfer = &trans->base.b;
   return ptr;
}

static void
zink_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;

   if (trans->base.b.usage & PIPE_MAP_WRITE) {
      struct zink_screen *screen = zink_screen(pctx->screen);
      struct zink_resource *m = trans->staging_res ? zink_resource(trans->staging_res) :
                                                     res;
      ASSERTED VkDeviceSize size, offset;
      if (m->obj->is_buffer) {
         size = box->width;
         offset = trans->offset + box->x;
      } else {
         size = box->width * box->height * util_format_get_blocksize(m->base.b.format);
         offset = trans->offset +
                  box->z * trans->depthPitch +
                  util_format_get_2d_size(m->base.b.format, trans->base.b.stride, box->y) +
                  util_format_get_stride(m->base.b.format, box->x);
         assert(offset + size <= res->obj->size);
      }
      if (!m->obj->coherent) {
         VkMappedMemoryRange range = init_mem_range(screen, m, m->obj->offset, m->obj->size);
         vkFlushMappedMemoryRanges(screen->dev, 1, &range);
      }
      if (trans->staging_res) {
         struct zink_resource *staging_res = zink_resource(trans->staging_res);

         if (ptrans->resource->target == PIPE_BUFFER)
            zink_copy_buffer(ctx, NULL, res, staging_res, box->x, box->x + trans->offset + m->obj->offset, box->width);
         else
            zink_transfer_copy_bufimage(ctx, res, staging_res, trans);
      }
   }
}

static void
zink_transfer_unmap(struct pipe_context *pctx,
                    struct pipe_transfer *ptrans)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;

   if (!(trans->base.b.usage & (PIPE_MAP_FLUSH_EXPLICIT | PIPE_MAP_COHERENT))) {
      zink_transfer_flush_region(pctx, ptrans, &ptrans->box);
   }

   if (trans->base.b.usage & PIPE_MAP_ONCE && !trans->staging_res && !screen->threaded)
      unmap_resource(screen, res);
   if ((trans->base.b.usage & PIPE_MAP_PERSISTENT) && !(trans->base.b.usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps--;

   if (trans->staging_res)
      pipe_resource_reference(&trans->staging_res, NULL);
   pipe_resource_reference(&trans->base.b.resource, NULL);

   if (trans->base.b.usage & PIPE_MAP_THREAD_SAFE) {
      free(trans);
   } else {
      /* Don't use pool_transfers_unsync. We are always in the driver
       * thread. Freeing an object into a different pool is allowed.
       */
      slab_free(&ctx->transfer_pool, ptrans);
   }
}

static void
zink_buffer_subdata(struct pipe_context *ctx, struct pipe_resource *buffer,
                    unsigned usage, unsigned offset, unsigned size, const void *data)
{
   struct pipe_transfer *transfer = NULL;
   struct pipe_box box;
   uint8_t *map = NULL;

   usage |= PIPE_MAP_WRITE;

   if (!(usage & PIPE_MAP_DIRECTLY))
      usage |= PIPE_MAP_DISCARD_RANGE;

   u_box_1d(offset, size, &box);
   map = zink_transfer_map(ctx, buffer, 0, usage, &box, &transfer);
   if (!map)
      return;

   memcpy(map, data, size);
   zink_transfer_unmap(ctx, transfer);
}

static struct pipe_resource *
zink_resource_get_separate_stencil(struct pipe_resource *pres)
{
   /* For packed depth-stencil, we treat depth as the primary resource
    * and store S8 as the "second plane" resource.
    */
   if (pres->next && pres->next->format == PIPE_FORMAT_S8_UINT)
      return pres->next;

   return NULL;

}

bool
zink_resource_object_init_storage(struct zink_context *ctx, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   /* base resource already has the cap */
   if (res->base.b.bind & PIPE_BIND_SHADER_IMAGE)
      return true;
   if (res->obj->is_buffer) {
      if (res->obj->sbuffer)
         return true;
      VkBufferCreateInfo bci = create_bci(screen, &res->base.b, res->base.b.bind | PIPE_BIND_SHADER_IMAGE);
      bci.size = res->obj->size;

      VkBuffer buffer;
      if (vkCreateBuffer(screen->dev, &bci, NULL, &buffer) != VK_SUCCESS)
         return false;
      vkBindBufferMemory(screen->dev, buffer, res->obj->mem, res->obj->offset);
      res->obj->sbuffer = res->obj->buffer;
      res->obj->buffer = buffer;
   } else {
      zink_fb_clears_apply_region(ctx, &res->base.b, (struct u_rect){0, res->base.b.width0, 0, res->base.b.height0});
      zink_resource_image_barrier(ctx, NULL, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0);
      res->base.b.bind |= PIPE_BIND_SHADER_IMAGE;
      struct zink_resource_object *old_obj = res->obj;
      struct zink_resource_object *new_obj = resource_object_create(screen, &res->base.b, NULL, &res->optimal_tiling);
      if (!new_obj) {
         debug_printf("new backing resource alloc failed!");
         res->base.b.bind &= ~PIPE_BIND_SHADER_IMAGE;
         return false;
      }
      struct zink_resource staging = *res;
      staging.obj = old_obj;
      res->obj = new_obj;
      zink_descriptor_set_refs_clear(&old_obj->desc_set_refs, old_obj);
      for (unsigned i = 0; i <= res->base.b.last_level; i++) {
         struct pipe_box box = {0, 0, 0,
                                u_minify(res->base.b.width0, i),
                                u_minify(res->base.b.height0, i), res->base.b.array_size};
         box.depth = util_num_layers(&res->base.b, i);
         ctx->base.resource_copy_region(&ctx->base, &res->base.b, i, 0, 0, 0, &staging.base.b, i, &box);
      }
      zink_resource_object_reference(screen, &old_obj, NULL);
   }

   if (res->bind_history & BITFIELD64_BIT(ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW)) {
      for (unsigned shader = 0; shader < PIPE_SHADER_TYPES; shader++) {
         if (res->bind_stages & (1 << shader)) {
            for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPE_IMAGE; i++) {
               if (res->bind_history & BITFIELD64_BIT(i))
                  zink_context_invalidate_descriptor_state(ctx, shader, i);
            }
         }
      }
   }
   if (res->obj->is_buffer)
      zink_resource_rebind(ctx, res);
   else {
      zink_rebind_framebuffer(ctx, res);
      /* this will be cleaned up in future commits */
      if (res->bind_history & BITFIELD_BIT(ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW)) {
         for (unsigned i = 0; i < PIPE_SHADER_TYPES; i++) {
            for (unsigned j = 0; j < ctx->num_sampler_views[i]; j++) {
               struct zink_sampler_view *sv = zink_sampler_view(ctx->sampler_views[i][j]);
               if (sv && sv->base.texture == &res->base.b) {
                   struct pipe_surface *psurf = &sv->image_view->base;
                   zink_rebind_surface(ctx, &psurf);
                   sv->image_view = zink_surface(psurf);
                   zink_context_invalidate_descriptor_state(ctx, i, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW);
               }
            }
         }
      }
   }

   return true;
}

void
zink_resource_setup_transfer_layouts(struct zink_context *ctx, struct zink_resource *src, struct zink_resource *dst)
{
   if (src == dst) {
      /* The Vulkan 1.1 specification says the following about valid usage
       * of vkCmdBlitImage:
       *
       * "srcImageLayout must be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
       *  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL"
       *
       * and:
       *
       * "dstImageLayout must be VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR,
       *  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL or VK_IMAGE_LAYOUT_GENERAL"
       *
       * Since we cant have the same image in two states at the same time,
       * we're effectively left with VK_IMAGE_LAYOUT_SHARED_PRESENT_KHR or
       * VK_IMAGE_LAYOUT_GENERAL. And since this isn't a present-related
       * operation, VK_IMAGE_LAYOUT_GENERAL seems most appropriate.
       */
      zink_resource_image_barrier(ctx, NULL, src,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   } else {
      zink_resource_image_barrier(ctx, NULL, src,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);

      zink_resource_image_barrier(ctx, NULL, dst,
                                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                  VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   }
}

void
zink_get_depth_stencil_resources(struct pipe_resource *res,
                                 struct zink_resource **out_z,
                                 struct zink_resource **out_s)
{
   if (!res) {
      if (out_z) *out_z = NULL;
      if (out_s) *out_s = NULL;
      return;
   }

   if (res->format != PIPE_FORMAT_S8_UINT) {
      if (out_z) *out_z = zink_resource(res);
      if (out_s) *out_s = zink_resource(zink_resource_get_separate_stencil(res));
   } else {
      if (out_z) *out_z = NULL;
      if (out_s) *out_s = zink_resource(res);
   }
}

static void
zink_resource_set_separate_stencil(struct pipe_resource *pres,
                                   struct pipe_resource *stencil)
{
   assert(util_format_has_depth(util_format_description(pres->format)));
   pipe_resource_reference(&pres->next, stencil);
}

static enum pipe_format
zink_resource_get_internal_format(struct pipe_resource *pres)
{
   struct zink_resource *res = zink_resource(pres);
   return res->internal_format;
}

static const struct u_transfer_vtbl transfer_vtbl = {
   .resource_create       = zink_resource_create,
   .resource_destroy      = zink_resource_destroy,
   .transfer_map          = zink_transfer_map,
   .transfer_unmap        = zink_transfer_unmap,
   .transfer_flush_region = zink_transfer_flush_region,
   .get_internal_format   = zink_resource_get_internal_format,
   .set_stencil           = zink_resource_set_separate_stencil,
   .get_stencil           = zink_resource_get_separate_stencil,
};

bool
zink_screen_resource_init(struct pipe_screen *pscreen)
{
   struct zink_screen *screen = zink_screen(pscreen);
   pscreen->resource_create = zink_resource_create;
   pscreen->resource_destroy = zink_resource_destroy;
   pscreen->transfer_helper = u_transfer_helper_create(&transfer_vtbl, true, true, false, false);

   if (screen->info.have_KHR_external_memory_fd) {
      pscreen->resource_get_handle = zink_resource_get_handle;
      pscreen->resource_from_handle = zink_resource_from_handle;
   }
   simple_mtx_init(&screen->mem_cache_mtx, mtx_plain);
   screen->resource_mem_cache = _mesa_hash_table_create(NULL, mem_hash, mem_equals);
   return !!screen->resource_mem_cache;
}

void
zink_context_resource_init(struct pipe_context *pctx)
{
   pctx->transfer_map = u_transfer_helper_deinterleave_transfer_map;
   pctx->transfer_unmap = u_transfer_helper_deinterleave_transfer_unmap;

   pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
   pctx->buffer_subdata = zink_buffer_subdata;
   pctx->texture_subdata = u_default_texture_subdata;
   pctx->invalidate_resource = zink_resource_invalidate;
}
