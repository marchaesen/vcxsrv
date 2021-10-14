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

#ifdef VK_USE_PLATFORM_METAL_EXT
#include "QuartzCore/CAMetalLayer.h"
#endif
#include "vulkan/wsi/wsi_common.h"

#include "util/slab.h"
#include "util/u_blitter.h"
#include "util/u_debug.h"
#include "util/format/u_format.h"
#include "util/u_transfer_helper.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_upload_mgr.h"
#include "util/os_file.h"
#include "frontend/sw_winsys.h"

#ifndef _WIN32
#define ZINK_USE_DMABUF
#endif

#ifdef ZINK_USE_DMABUF
#include <xf86drm.h>
#include "drm-uapi/drm_fourcc.h"
#else
/* these won't actually be used */
#define DRM_FORMAT_MOD_INVALID 0
#define DRM_FORMAT_MOD_LINEAR 0
#endif


static bool
equals_ivci(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(VkImageViewCreateInfo)) == 0;
}

static bool
equals_bvci(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(VkBufferViewCreateInfo)) == 0;
}

static void
zink_transfer_flush_region(struct pipe_context *pctx,
                           struct pipe_transfer *ptrans,
                           const struct pipe_box *box);

void
debug_describe_zink_resource_object(char *buf, const struct zink_resource_object *ptr)
{
   sprintf(buf, "zink_resource_object");
}

void
zink_destroy_resource_object(struct zink_screen *screen, struct zink_resource_object *obj)
{
   if (obj->is_buffer) {
      util_dynarray_foreach(&obj->tmp, VkBuffer, buffer)
         VKSCR(DestroyBuffer)(screen->dev, *buffer, NULL);
      VKSCR(DestroyBuffer)(screen->dev, obj->buffer, NULL);
   } else {
      VKSCR(DestroyImage)(screen->dev, obj->image, NULL);
   }

   util_dynarray_fini(&obj->tmp);
   zink_descriptor_set_refs_clear(&obj->desc_set_refs, obj);
   zink_bo_unref(screen, obj->bo);
   FREE(obj);
}

static void
zink_resource_destroy(struct pipe_screen *pscreen,
                      struct pipe_resource *pres)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   if (pres->target == PIPE_BUFFER) {
      util_range_destroy(&res->valid_buffer_range);
      util_idalloc_mt_free(&screen->buffer_ids, res->base.buffer_id_unique);
      assert(!_mesa_hash_table_num_entries(&res->bufferview_cache));
      simple_mtx_destroy(&res->bufferview_mtx);
   } else {
      assert(!_mesa_hash_table_num_entries(&res->surface_cache));
      simple_mtx_destroy(&res->surface_mtx);
   }
   /* no need to do anything for the caches, these objects own the resource lifetimes */

   zink_resource_object_reference(screen, &res->obj, NULL);
   zink_resource_object_reference(screen, &res->scanout_obj, NULL);
   threaded_resource_deinit(pres);
   ralloc_free(res);
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
   VkBufferCreateInfo bci;
   bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
   bci.pNext = NULL;
   bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   bci.queueFamilyIndexCount = 0;
   bci.pQueueFamilyIndices = NULL;
   bci.size = templ->width0;
   bci.flags = 0;
   assert(bci.size > 0);

   bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
               VK_BUFFER_USAGE_TRANSFER_DST_BIT |
               VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

   bci.usage |= VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
                VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_BUFFER_BIT_EXT |
                VK_BUFFER_USAGE_TRANSFORM_FEEDBACK_COUNTER_BUFFER_BIT_EXT;

   if (bind & PIPE_BIND_SHADER_IMAGE)
      bci.usage |= VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

   if (templ->flags & PIPE_RESOURCE_FLAG_SPARSE)
      bci.flags |= VK_BUFFER_CREATE_SPARSE_BINDING_BIT;
   return bci;
}

static bool
check_ici(struct zink_screen *screen, VkImageCreateInfo *ici, uint64_t modifier)
{
   VkImageFormatProperties image_props;
   VkResult ret;
   assert(modifier == DRM_FORMAT_MOD_INVALID ||
          (VKSCR(GetPhysicalDeviceImageFormatProperties2) && screen->info.have_EXT_image_drm_format_modifier));
   if (VKSCR(GetPhysicalDeviceImageFormatProperties2)) {
      VkImageFormatProperties2 props2;
      props2.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
      props2.pNext = NULL;
      VkPhysicalDeviceImageFormatInfo2 info;
      info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
      info.format = ici->format;
      info.type = ici->imageType;
      info.tiling = ici->tiling;
      info.usage = ici->usage;
      info.flags = ici->flags;

      VkPhysicalDeviceImageDrmFormatModifierInfoEXT mod_info;
      if (modifier != DRM_FORMAT_MOD_INVALID) {
         mod_info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
         mod_info.pNext = NULL;
         mod_info.drmFormatModifier = modifier;
         mod_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
         mod_info.queueFamilyIndexCount = 0;
         info.pNext = &mod_info;
      } else
         info.pNext = NULL;

      ret = VKSCR(GetPhysicalDeviceImageFormatProperties2)(screen->pdev, &info, &props2);
      image_props = props2.imageFormatProperties;
   } else
      ret = VKSCR(GetPhysicalDeviceImageFormatProperties)(screen->pdev, ici->format, ici->imageType,
                                                   ici->tiling, ici->usage, ici->flags, &image_props);
   return ret == VK_SUCCESS;
}

static VkImageUsageFlags
get_image_usage_for_feats(struct zink_screen *screen, VkFormatFeatureFlags feats, const struct pipe_resource *templ, unsigned bind)
{
   VkImageUsageFlags usage = 0;
   if (bind & ZINK_BIND_TRANSIENT)
      usage |= VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
   else {
      /* sadly, gallium doesn't let us know if it'll ever need this, so we have to assume */
      if (feats & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)
         usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
      if (feats & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)
         usage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
      if (feats & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT && (bind & (PIPE_BIND_LINEAR | PIPE_BIND_SHARED)) != (PIPE_BIND_LINEAR | PIPE_BIND_SHARED))
         usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

      if ((templ->nr_samples <= 1 || screen->info.feats.features.shaderStorageImageMultisample) &&
          (bind & PIPE_BIND_SHADER_IMAGE)) {
         if (feats & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;
      }
   }

   if (bind & PIPE_BIND_RENDER_TARGET) {
      if (feats & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) {
         usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
         if ((bind & (PIPE_BIND_LINEAR | PIPE_BIND_SHARED)) != (PIPE_BIND_LINEAR | PIPE_BIND_SHARED))
            usage |= VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT;
      } else
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

static VkFormatFeatureFlags
find_modifier_feats(const struct zink_modifier_prop *prop, uint64_t modifier, uint64_t *mod)
{
   for (unsigned j = 0; j < prop->drmFormatModifierCount; j++) {
      if (prop->pDrmFormatModifierProperties[j].drmFormatModifier == modifier) {
         *mod = modifier;
         return prop->pDrmFormatModifierProperties[j].drmFormatModifierTilingFeatures;
      }
   }
   return 0;
}

static VkImageUsageFlags
get_image_usage(struct zink_screen *screen, VkImageCreateInfo *ici, const struct pipe_resource *templ, unsigned bind, unsigned modifiers_count, const uint64_t *modifiers, uint64_t *mod)
{
   VkImageTiling tiling = ici->tiling;
   *mod = DRM_FORMAT_MOD_INVALID;
   if (modifiers_count) {
      bool have_linear = false;
      const struct zink_modifier_prop *prop = &screen->modifier_props[templ->format];
      assert(tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT);
      for (unsigned i = 0; i < modifiers_count; i++) {
         if (modifiers[i] == DRM_FORMAT_MOD_LINEAR) {
            have_linear = true;
            continue;
         }
         VkFormatFeatureFlags feats = find_modifier_feats(prop, modifiers[i], mod);
         if (feats) {
            VkImageUsageFlags usage = get_image_usage_for_feats(screen, feats, templ, bind);
            if (usage) {
               ici->usage = usage;
               if (check_ici(screen, ici, *mod))
                  return usage;
            }
         }
      }
      /* only try linear if no other options available */
      if (have_linear) {
         VkFormatFeatureFlags feats = find_modifier_feats(prop, DRM_FORMAT_MOD_LINEAR, mod);
         if (feats) {
            VkImageUsageFlags usage = get_image_usage_for_feats(screen, feats, templ, bind);
            if (usage) {
               ici->usage = usage;
               if (check_ici(screen, ici, *mod))
                  return usage;
            }
         }
      }
   } else
   {
      VkFormatProperties props = screen->format_props[templ->format];
      VkFormatFeatureFlags feats = tiling == VK_IMAGE_TILING_LINEAR ? props.linearTilingFeatures : props.optimalTilingFeatures;
      VkImageUsageFlags usage = get_image_usage_for_feats(screen, feats, templ, bind);
      if (usage) {
         ici->usage = usage;
         if (check_ici(screen, ici, *mod))
            return usage;
      }
   }
   *mod = DRM_FORMAT_MOD_INVALID;
   return 0;
}

static uint64_t
create_ici(struct zink_screen *screen, VkImageCreateInfo *ici, const struct pipe_resource *templ, bool dmabuf, unsigned bind, unsigned modifiers_count, const uint64_t *modifiers, bool *success)
{
   ici->sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
   ici->pNext = NULL;
   ici->flags = modifiers_count || dmabuf || bind & (PIPE_BIND_SCANOUT | PIPE_BIND_DEPTH_STENCIL) ? 0 : VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
   ici->usage = 0;
   ici->queueFamilyIndexCount = 0;

   switch (templ->target) {
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      ici->imageType = VK_IMAGE_TYPE_1D;
      break;

   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      ici->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      FALLTHROUGH;
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_RECT:
      ici->imageType = VK_IMAGE_TYPE_2D;
      break;

   case PIPE_TEXTURE_3D:
      ici->imageType = VK_IMAGE_TYPE_3D;
      if (bind & PIPE_BIND_RENDER_TARGET)
         ici->flags |= VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT;
      break;

   case PIPE_BUFFER:
      unreachable("PIPE_BUFFER should already be handled");

   default:
      unreachable("Unknown target");
   }

   if (screen->info.have_EXT_sample_locations &&
       bind & PIPE_BIND_DEPTH_STENCIL &&
       util_format_has_depth(util_format_description(templ->format)))
      ici->flags |= VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT;

   ici->format = zink_get_format(screen, templ->format);
   ici->extent.width = templ->width0;
   ici->extent.height = templ->height0;
   ici->extent.depth = templ->depth0;
   ici->mipLevels = templ->last_level + 1;
   ici->arrayLayers = MAX2(templ->array_size, 1);
   ici->samples = templ->nr_samples ? templ->nr_samples : VK_SAMPLE_COUNT_1_BIT;
   ici->tiling = modifiers_count ? VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT : bind & PIPE_BIND_LINEAR ? VK_IMAGE_TILING_LINEAR : VK_IMAGE_TILING_OPTIMAL;
   ici->sharingMode = VK_SHARING_MODE_EXCLUSIVE;
   ici->initialLayout = dmabuf ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED;

   if (templ->target == PIPE_TEXTURE_CUBE ||
       templ->target == PIPE_TEXTURE_CUBE_ARRAY ||
       (templ->target == PIPE_TEXTURE_2D_ARRAY &&
        ici->extent.width == ici->extent.height &&
        ici->arrayLayers >= 6)) {
      VkImageFormatProperties props;
      if (vkGetPhysicalDeviceImageFormatProperties(screen->pdev, ici->format,
                                                   ici->imageType, ici->tiling,
                                                   ici->usage, ici->flags |
                                                   VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT,
                                                   &props) == VK_SUCCESS) {
         if (props.sampleCounts & ici->samples)
            ici->flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
      }
   }

   if (templ->target == PIPE_TEXTURE_CUBE)
      ici->arrayLayers *= 6;

   if (templ->usage == PIPE_USAGE_STAGING &&
       templ->format != PIPE_FORMAT_B4G4R4A4_UNORM &&
       templ->format != PIPE_FORMAT_B4G4R4A4_UINT)
      ici->tiling = VK_IMAGE_TILING_LINEAR;

   bool first = true;
   bool tried[2] = {0};
   uint64_t mod = DRM_FORMAT_MOD_INVALID;
   while (!ici->usage) {
      if (!first) {
         switch (ici->tiling) {
         case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
            ici->tiling = VK_IMAGE_TILING_OPTIMAL;
            modifiers_count = 0;
            break;
         case VK_IMAGE_TILING_OPTIMAL:
            ici->tiling = VK_IMAGE_TILING_LINEAR;
            break;
         case VK_IMAGE_TILING_LINEAR:
            if (bind & PIPE_BIND_LINEAR) {
               *success = false;
               return DRM_FORMAT_MOD_INVALID;
            }
            ici->tiling = VK_IMAGE_TILING_OPTIMAL;
            break;
         default:
            unreachable("unhandled tiling mode");
         }
         if (tried[ici->tiling]) {
            *success = false;
               return DRM_FORMAT_MOD_INVALID;
         }
      }
      ici->usage = get_image_usage(screen, ici, templ, bind, modifiers_count, modifiers, &mod);
      first = false;
      if (ici->tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         tried[ici->tiling] = true;
   }

   *success = true;
   return mod;
}

static struct zink_resource_object *
resource_object_create(struct zink_screen *screen, const struct pipe_resource *templ, struct winsys_handle *whandle, bool *optimal_tiling,
                       const uint64_t *modifiers, int modifiers_count)
{
   struct zink_resource_object *obj = CALLOC_STRUCT(zink_resource_object);
   if (!obj)
      return NULL;

   VkMemoryRequirements reqs;
   VkMemoryPropertyFlags flags;
   bool need_dedicated = false;
   VkExternalMemoryHandleTypeFlags export_types = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;

   VkExternalMemoryHandleTypeFlags external = 0;
   if (whandle) {
      if (whandle->type == WINSYS_HANDLE_TYPE_FD)
         external = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      else
         unreachable("unknown handle type");
   }

   /* TODO: remove linear for wsi */
   bool scanout = templ->bind & PIPE_BIND_SCANOUT;
   bool shared = templ->bind & PIPE_BIND_SHARED;
   if (shared && screen->info.have_EXT_external_memory_dma_buf)
      export_types |= VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;

   pipe_reference_init(&obj->reference, 1);
   util_dynarray_init(&obj->tmp, NULL);
   util_dynarray_init(&obj->desc_set_refs.refs, NULL);
   if (templ->target == PIPE_BUFFER) {
      VkBufferCreateInfo bci = create_bci(screen, templ, templ->bind);

      if (VKSCR(CreateBuffer)(screen->dev, &bci, NULL, &obj->buffer) != VK_SUCCESS) {
         debug_printf("vkCreateBuffer failed\n");
         goto fail1;
      }

      VKSCR(GetBufferMemoryRequirements)(screen->dev, obj->buffer, &reqs);
      if (templ->usage == PIPE_USAGE_STAGING)
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      else if (templ->usage == PIPE_USAGE_STREAM)
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      else if (templ->usage == PIPE_USAGE_IMMUTABLE)
         flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      else
         flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      obj->is_buffer = true;
      obj->transfer_dst = true;
   } else {
      bool winsys_modifier = shared && whandle && whandle->modifier != DRM_FORMAT_MOD_INVALID;
      const uint64_t *ici_modifiers = winsys_modifier ? &whandle->modifier : modifiers;
      unsigned ici_modifier_count = winsys_modifier ? 1 : modifiers_count;
      bool success = false;
      VkImageCreateInfo ici;
      uint64_t mod = create_ici(screen, &ici, templ, !!external, templ->bind, ici_modifier_count, ici_modifiers, &success);
      VkExternalMemoryImageCreateInfo emici;
      VkImageDrmFormatModifierExplicitCreateInfoEXT idfmeci;
      VkImageDrmFormatModifierListCreateInfoEXT idfmlci;
      if (!success)
         goto fail1;

      if (shared || external) {
         emici.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
         emici.pNext = NULL;
         emici.handleTypes = export_types;
         ici.pNext = &emici;

         assert(ici.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT || mod != DRM_FORMAT_MOD_INVALID);
         if (winsys_modifier && ici.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
            assert(mod == whandle->modifier);
            idfmeci.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
            idfmeci.pNext = ici.pNext;
            idfmeci.drmFormatModifier = mod;

            /* TODO: store these values from other planes in their
             * respective zink_resource, and walk the next-pointers to
             * build up the planar array here instead.
             */
            assert(util_format_get_num_planes(templ->format) == 1);
            idfmeci.drmFormatModifierPlaneCount = 1;
            VkSubresourceLayout plane_layout = {
               .offset = whandle->offset,
               .size = 0,
               .rowPitch = whandle->stride,
               .arrayPitch = 0,
               .depthPitch = 0,
            };
            idfmeci.pPlaneLayouts = &plane_layout;

            ici.pNext = &idfmeci;
         } else if (ici.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
            idfmlci.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT;
            idfmlci.pNext = ici.pNext;
            idfmlci.drmFormatModifierCount = modifiers_count;
            idfmlci.pDrmFormatModifiers = modifiers;
            ici.pNext = &idfmlci;
         } else if (ici.tiling == VK_IMAGE_TILING_OPTIMAL) {
            // TODO: remove for wsi
            if (!external)
               ici.pNext = NULL;
            scanout = false;
            shared = false;
         }
      }

      if (optimal_tiling)
         *optimal_tiling = ici.tiling == VK_IMAGE_TILING_OPTIMAL;

      if (ici.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
         obj->transfer_dst = true;

      if (ici.tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT)
         obj->modifier_aspect = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT;

      struct wsi_image_create_info image_wsi_info = {
         VK_STRUCTURE_TYPE_WSI_IMAGE_CREATE_INFO_MESA,
         NULL,
         .scanout = true,
      };

      if ((screen->needs_mesa_wsi || screen->needs_mesa_flush_wsi) && scanout &&
          ici.tiling != VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
         image_wsi_info.pNext = ici.pNext;
         ici.pNext = &image_wsi_info;
      }

      VkResult result = VKSCR(CreateImage)(screen->dev, &ici, NULL, &obj->image);
      if (result != VK_SUCCESS) {
         debug_printf("vkCreateImage failed\n");
         goto fail1;
      }

      if (VKSCR(GetImageMemoryRequirements2)) {
         VkMemoryRequirements2 req2;
         req2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
         VkImageMemoryRequirementsInfo2 info2;
         info2.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
         info2.pNext = NULL;
         info2.image = obj->image;
         VkMemoryDedicatedRequirements ded;
         ded.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
         ded.pNext = NULL;
         req2.pNext = &ded;
         VKSCR(GetImageMemoryRequirements2)(screen->dev, &info2, &req2);
         memcpy(&reqs, &req2.memoryRequirements, sizeof(VkMemoryRequirements));
         need_dedicated = ded.prefersDedicatedAllocation || ded.requiresDedicatedAllocation;
      } else {
         VKSCR(GetImageMemoryRequirements)(screen->dev, obj->image, &reqs);
      }
      if (templ->usage == PIPE_USAGE_STAGING && ici.tiling == VK_IMAGE_TILING_LINEAR)
        flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      else
        flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

      obj->vkflags = ici.flags;
      obj->vkusage = ici.usage;
   }
   obj->alignment = reqs.alignment;

   if (templ->flags & PIPE_RESOURCE_FLAG_MAP_COHERENT || templ->usage == PIPE_USAGE_DYNAMIC)
      flags |= VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   else if (!(flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            templ->usage == PIPE_USAGE_STAGING)
      flags |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

   if (templ->bind & ZINK_BIND_TRANSIENT)
      flags |= VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT;

   VkMemoryAllocateInfo mai;
   enum zink_alloc_flag aflags = templ->flags & PIPE_RESOURCE_FLAG_SPARSE ? ZINK_ALLOC_SPARSE : 0;
   mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
   mai.pNext = NULL;
   mai.allocationSize = reqs.size;
   enum zink_heap heap = zink_heap_from_domain_flags(flags, aflags);
   mai.memoryTypeIndex = screen->heap_map[heap];
   if (unlikely(!(reqs.memoryTypeBits & BITFIELD_BIT(mai.memoryTypeIndex)))) {
      /* not valid based on reqs; demote to more compatible type */
      switch (heap) {
      case ZINK_HEAP_DEVICE_LOCAL_VISIBLE:
         heap = ZINK_HEAP_DEVICE_LOCAL;
         break;
      case ZINK_HEAP_HOST_VISIBLE_CACHED:
         heap = ZINK_HEAP_HOST_VISIBLE_COHERENT;
         break;
      default:
         break;
      }
      mai.memoryTypeIndex = screen->heap_map[heap];
      assert(reqs.memoryTypeBits & BITFIELD_BIT(mai.memoryTypeIndex));
   }

   VkMemoryType mem_type = screen->info.mem_props.memoryTypes[mai.memoryTypeIndex];
   obj->coherent = mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
   if (!(templ->flags & PIPE_RESOURCE_FLAG_SPARSE))
      obj->host_visible = mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;

   VkMemoryDedicatedAllocateInfo ded_alloc_info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
      .pNext = mai.pNext,
      .image = obj->image,
      .buffer = VK_NULL_HANDLE,
   };

   if (screen->info.have_KHR_dedicated_allocation && need_dedicated) {
      ded_alloc_info.pNext = mai.pNext;
      mai.pNext = &ded_alloc_info;
   }

   VkExportMemoryAllocateInfo emai;
   if (templ->bind & PIPE_BIND_SHARED && shared) {
      emai.sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
      emai.handleTypes = export_types;

      emai.pNext = mai.pNext;
      mai.pNext = &emai;
   }

   VkImportMemoryFdInfoKHR imfi = {
      VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
      NULL,
   };

   if (whandle) {
      imfi.pNext = NULL;
      imfi.handleType = external;
      imfi.fd = os_dupfd_cloexec(whandle->handle);
      if (imfi.fd < 0) {
         mesa_loge("ZINK: failed to dup dmabuf fd: %s\n", strerror(errno));
         goto fail1;
      }

      imfi.pNext = mai.pNext;
      mai.pNext = &imfi;
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

   unsigned alignment = MAX2(reqs.alignment, 256);
   if (templ->usage == PIPE_USAGE_STAGING && obj->is_buffer)
      alignment = MAX2(alignment, screen->info.props.limits.minMemoryMapAlignment);
   obj->alignment = alignment;
   obj->bo = zink_bo(zink_bo_create(screen, reqs.size, alignment, heap, mai.pNext ? ZINK_ALLOC_NO_SUBALLOC : 0, mai.pNext));
   if (!obj->bo)
     goto fail2;
   if (aflags == ZINK_ALLOC_SPARSE) {
      obj->size = templ->width0;
   } else {
      obj->offset = zink_bo_get_offset(obj->bo);
      obj->size = zink_bo_get_size(obj->bo);
   }

   if (templ->target == PIPE_BUFFER) {
      if (!(templ->flags & PIPE_RESOURCE_FLAG_SPARSE))
         if (VKSCR(BindBufferMemory)(screen->dev, obj->buffer, zink_bo_get_mem(obj->bo), obj->offset) != VK_SUCCESS)
            goto fail3;
   } else {
      if (VKSCR(BindImageMemory)(screen->dev, obj->image, zink_bo_get_mem(obj->bo), obj->offset) != VK_SUCCESS)
         goto fail3;
   }
   return obj;

fail3:
   zink_bo_unref(screen, obj->bo);

fail2:
   if (templ->target == PIPE_BUFFER)
      VKSCR(DestroyBuffer)(screen->dev, obj->buffer, NULL);
   else
      VKSCR(DestroyImage)(screen->dev, obj->image, NULL);
fail1:
   FREE(obj);
   return NULL;
}

static struct pipe_resource *
resource_create(struct pipe_screen *pscreen,
                const struct pipe_resource *templ,
                struct winsys_handle *whandle,
                unsigned external_usage,
                const uint64_t *modifiers, int modifiers_count)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = rzalloc(NULL, struct zink_resource);

   if (modifiers_count > 0) {
      /* for rebinds */
      res->modifiers_count = modifiers_count;
      res->modifiers = mem_dup(modifiers, modifiers_count * sizeof(uint64_t));
      if (!res->modifiers) {
         ralloc_free(res);
         return NULL;
      }
      /* TODO: remove this when multi-plane modifiers are supported */
      const struct zink_modifier_prop *prop = &screen->modifier_props[templ->format];
      for (unsigned i = 0; i < modifiers_count; i++) {
         for (unsigned j = 0; j < prop->drmFormatModifierCount; j++) {
            if (prop->pDrmFormatModifierProperties[j].drmFormatModifier == modifiers[i]) {
               if (prop->pDrmFormatModifierProperties[j].drmFormatModifierPlaneCount != 1)
                  res->modifiers[i] = DRM_FORMAT_MOD_INVALID;
               break;
            }
         }
      }
   }

   res->base.b = *templ;

   threaded_resource_init(&res->base.b);
   pipe_reference_init(&res->base.b.reference, 1);
   res->base.b.screen = pscreen;

   bool optimal_tiling = false;
   struct pipe_resource templ2 = *templ;
   unsigned scanout_flags = templ->bind & (PIPE_BIND_SCANOUT | PIPE_BIND_SHARED);
   if (!(templ->bind & PIPE_BIND_LINEAR))
      templ2.bind &= ~scanout_flags;
   res->obj = resource_object_create(screen, &templ2, whandle, &optimal_tiling, NULL, 0);
   if (!res->obj) {
      free(res->modifiers);
      ralloc_free(res);
      return NULL;
   }

   res->internal_format = templ->format;
   if (templ->target == PIPE_BUFFER) {
      util_range_init(&res->valid_buffer_range);
      if (!screen->resizable_bar && templ->width0 >= 8196) {
         /* We don't want to evict buffers from VRAM by mapping them for CPU access,
          * because they might never be moved back again. If a buffer is large enough,
          * upload data by copying from a temporary GTT buffer. 8K might not seem much,
          * but there can be 100000 buffers.
          *
          * This tweak improves performance for viewperf.
          */
         res->base.b.flags |= PIPE_RESOURCE_FLAG_DONT_MAP_DIRECTLY;
      }
   } else {
      res->format = zink_get_format(screen, templ->format);
      res->dmabuf_acquire = whandle && whandle->type == WINSYS_HANDLE_TYPE_FD;
      res->layout = res->dmabuf_acquire ? VK_IMAGE_LAYOUT_PREINITIALIZED : VK_IMAGE_LAYOUT_UNDEFINED;
      res->optimal_tiling = optimal_tiling;
      res->aspect = aspect_from_format(templ->format);
      if (scanout_flags && optimal_tiling) {
         // TODO: remove for wsi
         templ2 = res->base.b;
         templ2.bind = scanout_flags | PIPE_BIND_LINEAR;
         res->scanout_obj = resource_object_create(screen, &templ2, whandle, &optimal_tiling, res->modifiers, res->modifiers_count);
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
   if (res->obj->is_buffer) {
      res->base.buffer_id_unique = util_idalloc_mt_alloc(&screen->buffer_ids);
      _mesa_hash_table_init(&res->bufferview_cache, res, NULL, equals_bvci);
      simple_mtx_init(&res->bufferview_mtx, mtx_plain);
   } else {
      _mesa_hash_table_init(&res->surface_cache, res, NULL, equals_ivci);
      simple_mtx_init(&res->surface_mtx, mtx_plain);
   }
   return &res->base.b;
}

static struct pipe_resource *
zink_resource_create(struct pipe_screen *pscreen,
                     const struct pipe_resource *templ)
{
   return resource_create(pscreen, templ, NULL, 0, NULL, 0);
}

static struct pipe_resource *
zink_resource_create_with_modifiers(struct pipe_screen *pscreen, const struct pipe_resource *templ,
                                    const uint64_t *modifiers, int modifiers_count)
{
   return resource_create(pscreen, templ, NULL, 0, modifiers, modifiers_count);
}

static bool
zink_resource_get_param(struct pipe_screen *pscreen, struct pipe_context *pctx,
                        struct pipe_resource *pres,
                        unsigned plane,
                        unsigned layer,
                        unsigned level,
                        enum pipe_resource_param param,
                        unsigned handle_usage,
                        uint64_t *value)
{
   struct zink_screen *screen = zink_screen(pscreen);
   struct zink_resource *res = zink_resource(pres);
   //TODO: remove for wsi
   struct zink_resource_object *obj = res->scanout_obj ? res->scanout_obj : res->obj;
   VkImageAspectFlags aspect = obj->modifier_aspect ? obj->modifier_aspect : res->aspect;
   struct winsys_handle whandle;
   switch (param) {
   case PIPE_RESOURCE_PARAM_NPLANES:
      /* not yet implemented */
      *value = 1;
      break;

   case PIPE_RESOURCE_PARAM_STRIDE: {
      VkImageSubresource sub_res = {0};
      VkSubresourceLayout sub_res_layout = {0};

      sub_res.aspectMask = aspect;

      VKSCR(GetImageSubresourceLayout)(screen->dev, obj->image, &sub_res, &sub_res_layout);

      *value = sub_res_layout.rowPitch;
      break;
   }

   case PIPE_RESOURCE_PARAM_OFFSET: {
         VkImageSubresource isr = {
            aspect,
            level,
            layer
         };
         VkSubresourceLayout srl;
         VKSCR(GetImageSubresourceLayout)(screen->dev, obj->image, &isr, &srl);
         *value = srl.offset;
         break;
   }

   case PIPE_RESOURCE_PARAM_MODIFIER: {
      *value = DRM_FORMAT_MOD_INVALID;
      if (!screen->info.have_EXT_image_drm_format_modifier)
         return false;
      if (!res->modifiers)
         return false;
      VkImageDrmFormatModifierPropertiesEXT prop;
      prop.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
      prop.pNext = NULL;
      if (VKSCR(GetImageDrmFormatModifierPropertiesEXT)(screen->dev, obj->image, &prop) == VK_SUCCESS)
         *value = prop.drmFormatModifier;
      break;
   }

   case PIPE_RESOURCE_PARAM_LAYER_STRIDE: {
         VkImageSubresource isr = {
            aspect,
            level,
            layer
         };
         VkSubresourceLayout srl;
         VKSCR(GetImageSubresourceLayout)(screen->dev, obj->image, &isr, &srl);
         if (res->base.b.target == PIPE_TEXTURE_3D)
            *value = srl.depthPitch;
         else
            *value = srl.arrayPitch;
         break;
   }

   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED:
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS:
   case PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD: {
      memset(&whandle, 0, sizeof(whandle));
      if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_SHARED)
         whandle.type = WINSYS_HANDLE_TYPE_SHARED;
      else if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_KMS)
         whandle.type = WINSYS_HANDLE_TYPE_KMS;
      else if (param == PIPE_RESOURCE_PARAM_HANDLE_TYPE_FD)
         whandle.type = WINSYS_HANDLE_TYPE_FD;

      if (!pscreen->resource_get_handle(pscreen, pctx, pres, &whandle, handle_usage))
         return false;

      *value = whandle.handle;
      break;
   }
   }
   return true;
}

static bool
zink_resource_get_handle(struct pipe_screen *pscreen,
                         struct pipe_context *context,
                         struct pipe_resource *tex,
                         struct winsys_handle *whandle,
                         unsigned usage)
{
   if (whandle->type == WINSYS_HANDLE_TYPE_FD || whandle->type == WINSYS_HANDLE_TYPE_KMS) {
#ifdef ZINK_USE_DMABUF
      struct zink_resource *res = zink_resource(tex);
      struct zink_screen *screen = zink_screen(pscreen);
      //TODO: remove for wsi
      struct zink_resource_object *obj = res->scanout_obj ? res->scanout_obj : res->obj;

      VkMemoryGetFdInfoKHR fd_info = {0};
      int fd;
      fd_info.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
      //TODO: remove for wsi
      fd_info.memory = zink_bo_get_mem(obj->bo);
      if (whandle->type == WINSYS_HANDLE_TYPE_FD)
         fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      else
         fd_info.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
      VkResult result = VKSCR(GetMemoryFdKHR)(screen->dev, &fd_info, &fd);
      if (result != VK_SUCCESS)
         return false;
      if (whandle->type == WINSYS_HANDLE_TYPE_KMS) {
         uint32_t h;
         bool success = drmPrimeFDToHandle(screen->drm_fd, fd, &h) == 0;
         close(fd);
         if (!success)
            return false;
         fd = h;
      }
      whandle->handle = fd;
      uint64_t value;
      zink_resource_get_param(pscreen, context, tex, 0, 0, 0, PIPE_RESOURCE_PARAM_MODIFIER, 0, &value);
      whandle->modifier = value;
      zink_resource_get_param(pscreen, context, tex, 0, 0, 0, PIPE_RESOURCE_PARAM_OFFSET, 0, &value);
      whandle->offset = value;
      zink_resource_get_param(pscreen, context, tex, 0, 0, 0, PIPE_RESOURCE_PARAM_STRIDE, 0, &value);
      whandle->stride = value;
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
   if (whandle->modifier != DRM_FORMAT_MOD_INVALID &&
       !zink_screen(pscreen)->info.have_EXT_image_drm_format_modifier)
      return NULL;

   /* ignore any AUX planes, as well as planar formats */
   if (templ->format == PIPE_FORMAT_NONE ||
       util_format_get_num_planes(templ->format) != 1)
      return NULL;

   uint64_t modifier = DRM_FORMAT_MOD_INVALID;
   int modifier_count = 0;
   if (whandle->modifier != DRM_FORMAT_MOD_INVALID) {
      modifier = whandle->modifier;
      modifier_count = 1;
   }
   return resource_create(pscreen, templ, whandle, usage, &modifier, modifier_count);
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

   if (res->so_valid)
      ctx->dirty_so_targets = true;
   /* force counter buffer reset */
   res->so_valid = false;

   util_range_set_empty(&res->valid_buffer_range);
   if (!zink_resource_has_usage(res))
      return false;

   struct zink_resource_object *old_obj = res->obj;
   struct zink_resource_object *new_obj = resource_object_create(screen, &res->base.b, NULL, NULL, NULL, 0);
   if (!new_obj) {
      debug_printf("new backing resource alloc failed!");
      return false;
   }
   /* this ref must be transferred before rebind or else BOOM */
   zink_batch_reference_resource_move(&ctx->batch, res);
   res->obj = new_obj;
   zink_resource_rebind(ctx, res);
   zink_descriptor_set_refs_clear(&old_obj->desc_set_refs, old_obj);
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
      box.x = trans->offset;

   if (dst->obj->transfer_dst)
      zink_copy_image_buffer(ctx, dst, src, trans->base.b.level, buf2img ? x : 0,
                              box.y, box.z, trans->base.b.level, &box, trans->base.b.usage);
   else
      util_blitter_copy_texture(ctx->blitter, &dst->base.b, trans->base.b.level,
                                x, box.y, box.z, &src->base.b,
                                0, &box);
}

ALWAYS_INLINE static void
align_offset_size(const VkDeviceSize alignment, VkDeviceSize *offset, VkDeviceSize *size, VkDeviceSize obj_size)
{
   VkDeviceSize align = *offset % alignment;
   if (alignment - 1 > *offset)
      *offset = 0;
   else
      *offset -= align, *size += align;
   align = alignment - (*size % alignment);
   if (*offset + *size + align > obj_size)
      *size = obj_size - *offset;
   else
      *size += align;
}

VkMappedMemoryRange
zink_resource_init_mem_range(struct zink_screen *screen, struct zink_resource_object *obj, VkDeviceSize offset, VkDeviceSize size)
{
   assert(obj->size);
   align_offset_size(screen->info.props.limits.nonCoherentAtomSize, &offset, &size, obj->size);
   VkMappedMemoryRange range = {
      VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
      NULL,
      zink_bo_get_mem(obj->bo),
      offset,
      size
   };
   assert(range.size);
   return range;
}

static void *
map_resource(struct zink_screen *screen, struct zink_resource *res)
{
   assert(res->obj->host_visible);
   return zink_bo_map(screen, res->obj->bo);
}

static void
unmap_resource(struct zink_screen *screen, struct zink_resource *res)
{
   zink_bo_unmap(screen, res->obj->bo);
}

static struct zink_transfer *
create_transfer(struct zink_context *ctx, struct pipe_resource *pres, unsigned usage, const struct pipe_box *box)
{
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

   trans->base.b.usage = usage;
   trans->base.b.box = *box;
   return trans;
}

static void
destroy_transfer(struct zink_context *ctx, struct zink_transfer *trans)
{
   if (trans->base.b.usage & PIPE_MAP_THREAD_SAFE) {
      free(trans);
   } else {
      /* Don't use pool_transfers_unsync. We are always in the driver
       * thread. Freeing an object into a different pool is allowed.
       */
      slab_free(&ctx->transfer_pool, trans);
   }
}

static void *
zink_buffer_map(struct pipe_context *pctx,
                    struct pipe_resource *pres,
                    unsigned level,
                    unsigned usage,
                    const struct pipe_box *box,
                    struct pipe_transfer **transfer)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);
   struct zink_transfer *trans = create_transfer(ctx, pres, usage, box);
   if (!trans)
      return NULL;

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

   /* If a buffer in VRAM is too large and the range is discarded, don't
    * map it directly. This makes sure that the buffer stays in VRAM.
    */
   bool force_discard_range = false;
   if (usage & (PIPE_MAP_DISCARD_WHOLE_RESOURCE | PIPE_MAP_DISCARD_RANGE) &&
       !(usage & PIPE_MAP_PERSISTENT) &&
       res->base.b.flags & PIPE_RESOURCE_FLAG_DONT_MAP_DIRECTLY) {
      usage &= ~(PIPE_MAP_DISCARD_WHOLE_RESOURCE | PIPE_MAP_UNSYNCHRONIZED);
      usage |= PIPE_MAP_DISCARD_RANGE;
      force_discard_range = true;
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

   if (usage & PIPE_MAP_DISCARD_RANGE &&
        (!res->obj->host_visible ||
        !(usage & (PIPE_MAP_UNSYNCHRONIZED | PIPE_MAP_PERSISTENT)))) {

      /* Check if mapping this buffer would cause waiting for the GPU.
       */

      if (!res->obj->host_visible || force_discard_range ||
          !zink_resource_usage_check_completion(screen, res, ZINK_RESOURCE_ACCESS_RW)) {
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
         trans->offset = offset + box->x;
         usage |= PIPE_MAP_UNSYNCHRONIZED;
         ptr = ((uint8_t *)ptr) + box->x;
      } else {
         /* At this point, the buffer is always idle (we checked it above). */
         usage |= PIPE_MAP_UNSYNCHRONIZED;
      }
   } else if (usage & PIPE_MAP_DONTBLOCK) {
      /* sparse/device-local will always need to wait since it has to copy */
      if (!res->obj->host_visible)
         goto success;
      if (!zink_resource_usage_check_completion(screen, res, ZINK_RESOURCE_ACCESS_WRITE))
         goto success;
      usage |= PIPE_MAP_UNSYNCHRONIZED;
   } else if (!(usage & PIPE_MAP_UNSYNCHRONIZED) &&
              (((usage & PIPE_MAP_READ) && !(usage & PIPE_MAP_PERSISTENT) && res->base.b.usage != PIPE_USAGE_STAGING) || !res->obj->host_visible)) {
      assert(!(usage & (TC_TRANSFER_MAP_THREADED_UNSYNC | PIPE_MAP_THREAD_SAFE)));
      if (!res->obj->host_visible || !(usage & PIPE_MAP_ONCE)) {
         trans->offset = box->x % screen->info.props.limits.minMemoryMapAlignment;
         trans->staging_res = pipe_buffer_create(&screen->base, PIPE_BIND_LINEAR, PIPE_USAGE_STAGING, box->width + trans->offset);
         if (!trans->staging_res)
            goto fail;
         struct zink_resource *staging_res = zink_resource(trans->staging_res);
         zink_copy_buffer(ctx, staging_res, res, trans->offset, box->x, box->width);
         res = staging_res;
         usage &= ~PIPE_MAP_UNSYNCHRONIZED;
         ptr = map_resource(screen, res);
         ptr = ((uint8_t *)ptr) + trans->offset;
      }
   }

   if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
      if (usage & PIPE_MAP_WRITE)
         zink_resource_usage_wait(ctx, res, ZINK_RESOURCE_ACCESS_RW);
      else
         zink_resource_usage_wait(ctx, res, ZINK_RESOURCE_ACCESS_WRITE);
      res->obj->access = 0;
      res->obj->access_stage = 0;
   }

   if (!ptr) {
      /* if writing to a streamout buffer, ensure synchronization next time it's used */
      if (usage & PIPE_MAP_WRITE && res->so_valid) {
         ctx->dirty_so_targets = true;
         /* force counter buffer reset */
         res->so_valid = false;
      }
      ptr = map_resource(screen, res);
      if (!ptr)
         goto fail;
      ptr = ((uint8_t *)ptr) + box->x;
   }

   if (!res->obj->coherent
#if defined(MVK_VERSION)
      // Work around for MoltenVk limitation specifically on coherent memory
      // MoltenVk returns blank memory ranges when there should be data present
      // This is a known limitation of MoltenVK.
      // See https://github.com/KhronosGroup/MoltenVK/blob/master/Docs/MoltenVK_Runtime_UserGuide.md#known-moltenvk-limitations

       || screen->instance_info.have_MVK_moltenvk
#endif
      ) {
      VkDeviceSize size = box->width;
      VkDeviceSize offset = res->obj->offset + trans->offset;
      VkMappedMemoryRange range = zink_resource_init_mem_range(screen, res->obj, offset, size);
      if (VKSCR(InvalidateMappedMemoryRanges)(screen->dev, 1, &range) != VK_SUCCESS) {
         zink_bo_unmap(screen, res->obj->bo);
         goto fail;
      }
   }
   trans->base.b.usage = usage;
   if (usage & PIPE_MAP_WRITE)
      util_range_add(&res->base.b, &res->valid_buffer_range, box->x, box->x + box->width);
   if ((usage & PIPE_MAP_PERSISTENT) && !(usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps++;

success:
   *transfer = &trans->base.b;
   return ptr;

fail:
   destroy_transfer(ctx, trans);
   return NULL;
}

static void *
zink_image_map(struct pipe_context *pctx,
                  struct pipe_resource *pres,
                  unsigned level,
                  unsigned usage,
                  const struct pipe_box *box,
                  struct pipe_transfer **transfer)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);
   struct zink_transfer *trans = create_transfer(ctx, pres, usage, box);
   if (!trans)
      return NULL;

   trans->base.b.level = level;

   void *ptr;
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
         goto fail;

      struct zink_resource *staging_res = zink_resource(trans->staging_res);

      if (usage & PIPE_MAP_READ) {
         /* force multi-context sync */
         if (zink_resource_usage_is_unflushed_write(res))
            zink_resource_usage_wait(ctx, res, ZINK_RESOURCE_ACCESS_WRITE);
         zink_transfer_copy_bufimage(ctx, staging_res, res, trans);
         /* need to wait for rendering to finish */
         zink_fence_wait(pctx);
      }

      ptr = map_resource(screen, staging_res);
   } else {
      assert(!res->optimal_tiling);
      ptr = map_resource(screen, res);
      if (!ptr)
         goto fail;
      if (zink_resource_has_usage(res)) {
         if (usage & PIPE_MAP_WRITE)
            zink_fence_wait(pctx);
         else
            zink_resource_usage_wait(ctx, res, ZINK_RESOURCE_ACCESS_WRITE);
      }
      VkImageSubresource isr = {
         res->obj->modifier_aspect ? res->obj->modifier_aspect : res->aspect,
         level,
         0
      };
      VkSubresourceLayout srl;
      VKSCR(GetImageSubresourceLayout)(screen->dev, res->obj->image, &isr, &srl);
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
         VkDeviceSize size = (VkDeviceSize)box->width * box->height * desc->block.bits / 8;
         VkMappedMemoryRange range = zink_resource_init_mem_range(screen, res->obj, res->obj->offset + offset, size);
         VKSCR(FlushMappedMemoryRanges)(screen->dev, 1, &range);
      }
      ptr = ((uint8_t *)ptr) + offset;
   }
   if (!ptr)
      goto fail;

   if (sizeof(void*) == 4)
      trans->base.b.usage |= ZINK_MAP_TEMPORARY;
   if ((usage & PIPE_MAP_PERSISTENT) && !(usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps++;

   *transfer = &trans->base.b;
   return ptr;

fail:
   destroy_transfer(ctx, trans);
   return NULL;
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
         offset = trans->offset;
      } else {
         size = (VkDeviceSize)box->width * box->height * util_format_get_blocksize(m->base.b.format);
         offset = trans->offset +
                  box->z * trans->depthPitch +
                  util_format_get_2d_size(m->base.b.format, trans->base.b.stride, box->y) +
                  util_format_get_stride(m->base.b.format, box->x);
         assert(offset + size <= res->obj->size);
      }
      if (!m->obj->coherent) {
         VkMappedMemoryRange range = zink_resource_init_mem_range(screen, m->obj, m->obj->offset, m->obj->size);
         VKSCR(FlushMappedMemoryRanges)(screen->dev, 1, &range);
      }
      if (trans->staging_res) {
         struct zink_resource *staging_res = zink_resource(trans->staging_res);

         if (ptrans->resource->target == PIPE_BUFFER)
            zink_copy_buffer(ctx, res, staging_res, box->x, offset, box->width);
         else
            zink_transfer_copy_bufimage(ctx, res, staging_res, trans);
      }
   }
}

static void
transfer_unmap(struct pipe_context *pctx, struct pipe_transfer *ptrans)
{
   struct zink_context *ctx = zink_context(pctx);
   struct zink_resource *res = zink_resource(ptrans->resource);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;

   if (!(trans->base.b.usage & (PIPE_MAP_FLUSH_EXPLICIT | PIPE_MAP_COHERENT))) {
      zink_transfer_flush_region(pctx, ptrans, &ptrans->box);
   }

   if ((trans->base.b.usage & PIPE_MAP_PERSISTENT) && !(trans->base.b.usage & PIPE_MAP_COHERENT))
      res->obj->persistent_maps--;

   if (trans->staging_res)
      pipe_resource_reference(&trans->staging_res, NULL);
   pipe_resource_reference(&trans->base.b.resource, NULL);

   destroy_transfer(ctx, trans);
}

static void
do_transfer_unmap(struct zink_screen *screen, struct zink_transfer *trans)
{
   struct zink_resource *res = zink_resource(trans->staging_res);
   if (!res)
      res = zink_resource(trans->base.b.resource);
   unmap_resource(screen, res);
}

static void
zink_buffer_unmap(struct pipe_context *pctx, struct pipe_transfer *ptrans)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;
   if (trans->base.b.usage & PIPE_MAP_ONCE && !trans->staging_res)
      do_transfer_unmap(screen, trans);
   transfer_unmap(pctx, ptrans);
}

static void
zink_image_unmap(struct pipe_context *pctx, struct pipe_transfer *ptrans)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_transfer *trans = (struct zink_transfer *)ptrans;
   if (sizeof(void*) == 4)
      do_transfer_unmap(screen, trans);
   transfer_unmap(pctx, ptrans);
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
   map = zink_buffer_map(ctx, buffer, 0, usage, &box, &transfer);
   if (!map)
      return;

   memcpy(map, data, size);
   zink_buffer_unmap(ctx, transfer);
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

VkBuffer
zink_resource_tmp_buffer(struct zink_screen *screen, struct zink_resource *res, unsigned offset_add, unsigned add_binds, unsigned *offset_out)
{
   VkBufferCreateInfo bci = create_bci(screen, &res->base.b, res->base.b.bind | add_binds);
   VkDeviceSize size = bci.size - offset_add;
   VkDeviceSize offset = offset_add;
   if (offset_add) {
      assert(bci.size > offset_add);

      align_offset_size(res->obj->alignment, &offset, &size, bci.size);
   }
   bci.size = size;

   VkBuffer buffer;
   if (VKSCR(CreateBuffer)(screen->dev, &bci, NULL, &buffer) != VK_SUCCESS)
      return VK_NULL_HANDLE;
   VKSCR(BindBufferMemory)(screen->dev, buffer, zink_bo_get_mem(res->obj->bo), res->obj->offset + offset);
   if (offset_out)
      *offset_out = offset_add - offset;
   return buffer;
}

bool
zink_resource_object_init_storage(struct zink_context *ctx, struct zink_resource *res)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   /* base resource already has the cap */
   if (res->base.b.bind & PIPE_BIND_SHADER_IMAGE)
      return true;
   if (res->obj->is_buffer) {
      if (res->base.b.bind & PIPE_BIND_SHADER_IMAGE)
         return true;

      VkBuffer buffer = zink_resource_tmp_buffer(screen, res, 0, PIPE_BIND_SHADER_IMAGE, NULL);
      if (!buffer)
         return false;
      util_dynarray_append(&res->obj->tmp, VkBuffer, res->obj->buffer);
      res->obj->buffer = buffer;
      res->base.b.bind |= PIPE_BIND_SHADER_IMAGE;
   } else {
      zink_fb_clears_apply_region(ctx, &res->base.b, (struct u_rect){0, res->base.b.width0, 0, res->base.b.height0});
      zink_resource_image_barrier(ctx, res, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 0);
      res->base.b.bind |= PIPE_BIND_SHADER_IMAGE;
      struct zink_resource_object *old_obj = res->obj;
      struct zink_resource_object *new_obj = resource_object_create(screen, &res->base.b, NULL, &res->optimal_tiling, res->modifiers, res->modifiers_count);
      if (!new_obj) {
         debug_printf("new backing resource alloc failed!");
         res->base.b.bind &= ~PIPE_BIND_SHADER_IMAGE;
         return false;
      }
      struct zink_resource staging = *res;
      staging.obj = old_obj;
      bool needs_unref = true;
      if (zink_resource_has_usage(res)) {
         zink_batch_reference_resource_move(&ctx->batch, res);
         needs_unref = false;
      }
      res->obj = new_obj;
      zink_descriptor_set_refs_clear(&old_obj->desc_set_refs, old_obj);
      for (unsigned i = 0; i <= res->base.b.last_level; i++) {
         struct pipe_box box = {0, 0, 0,
                                u_minify(res->base.b.width0, i),
                                u_minify(res->base.b.height0, i), res->base.b.array_size};
         box.depth = util_num_layers(&res->base.b, i);
         ctx->base.resource_copy_region(&ctx->base, &res->base.b, i, 0, 0, 0, &staging.base.b, i, &box);
      }
      if (needs_unref)
         zink_resource_object_reference(screen, &old_obj, NULL);
   }

   zink_resource_rebind(ctx, res);

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
      zink_resource_image_barrier(ctx, src,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);
   } else {
      zink_resource_image_barrier(ctx, src,
                                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                  VK_ACCESS_TRANSFER_READ_BIT,
                                  VK_PIPELINE_STAGE_TRANSFER_BIT);

      zink_resource_image_barrier(ctx, dst,
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
   .transfer_map          = zink_image_map,
   .transfer_unmap        = zink_image_unmap,
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
   pscreen->resource_create_with_modifiers = zink_resource_create_with_modifiers;
   pscreen->resource_destroy = zink_resource_destroy;
   pscreen->transfer_helper = u_transfer_helper_create(&transfer_vtbl, true, true, false, false);

   if (screen->info.have_KHR_external_memory_fd) {
      pscreen->resource_get_handle = zink_resource_get_handle;
      pscreen->resource_from_handle = zink_resource_from_handle;
   }
   pscreen->resource_get_param = zink_resource_get_param;
   return true;
}

void
zink_context_resource_init(struct pipe_context *pctx)
{
   pctx->buffer_map = zink_buffer_map;
   pctx->buffer_unmap = zink_buffer_unmap;
   pctx->texture_map = u_transfer_helper_deinterleave_transfer_map;
   pctx->texture_unmap = u_transfer_helper_deinterleave_transfer_unmap;

   pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
   pctx->buffer_subdata = zink_buffer_subdata;
   pctx->texture_subdata = u_default_texture_subdata;
   pctx->invalidate_resource = zink_resource_invalidate;
}
