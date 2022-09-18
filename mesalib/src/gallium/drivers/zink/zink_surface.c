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

#include "zink_context.h"
#include "zink_framebuffer.h"
#include "zink_resource.h"
#include "zink_screen.h"
#include "zink_surface.h"
#include "zink_kopper.h"

#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"

VkImageViewCreateInfo
create_ivci(struct zink_screen *screen,
            struct zink_resource *res,
            const struct pipe_surface *templ,
            enum pipe_texture_target target)
{
   VkImageViewCreateInfo ivci;
   /* zero holes since this is hashed */
   memset(&ivci, 0, sizeof(VkImageViewCreateInfo));
   ivci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
   ivci.image = res->obj->image;

   switch (target) {
   case PIPE_TEXTURE_1D:
      ivci.viewType = res->need_2D ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D;
      break;

   case PIPE_TEXTURE_1D_ARRAY:
      ivci.viewType = res->need_2D ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
      break;

   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_RECT:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
      break;

   case PIPE_TEXTURE_2D_ARRAY:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
      break;

   case PIPE_TEXTURE_CUBE:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
      break;

   case PIPE_TEXTURE_CUBE_ARRAY:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
      break;

   case PIPE_TEXTURE_3D:
      ivci.viewType = VK_IMAGE_VIEW_TYPE_3D;
      break;

   default:
      unreachable("unsupported target");
   }

   ivci.format = zink_get_format(screen, templ->format);
   assert(ivci.format != VK_FORMAT_UNDEFINED);

   /* TODO: it's currently illegal to use non-identity swizzles for framebuffer attachments,
    * but if that ever changes, this will be useful
   const struct util_format_description *desc = util_format_description(templ->format);
   ivci.components.r = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_X));
   ivci.components.g = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_Y));
   ivci.components.b = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_Z));
   ivci.components.a = zink_component_mapping(zink_clamp_void_swizzle(desc, PIPE_SWIZZLE_W));
   */
   ivci.components.r = VK_COMPONENT_SWIZZLE_R;
   ivci.components.g = VK_COMPONENT_SWIZZLE_G;
   ivci.components.b = VK_COMPONENT_SWIZZLE_B;
   ivci.components.a = VK_COMPONENT_SWIZZLE_A;

   ivci.subresourceRange.aspectMask = res->aspect;
   ivci.subresourceRange.baseMipLevel = templ->u.tex.level;
   ivci.subresourceRange.levelCount = 1;
   ivci.subresourceRange.baseArrayLayer = templ->u.tex.first_layer;
   ivci.subresourceRange.layerCount = 1 + templ->u.tex.last_layer - templ->u.tex.first_layer;
   assert(ivci.viewType != VK_IMAGE_VIEW_TYPE_3D || ivci.subresourceRange.baseArrayLayer == 0);
   assert(ivci.viewType != VK_IMAGE_VIEW_TYPE_3D || ivci.subresourceRange.layerCount == 1);
   ivci.viewType = zink_surface_clamp_viewtype(ivci.viewType, templ->u.tex.first_layer, templ->u.tex.last_layer, res->base.b.array_size);

   return ivci;
}

static void
init_surface_info(struct zink_surface *surface, struct zink_resource *res, VkImageViewCreateInfo *ivci)
{
   VkImageViewUsageCreateInfo *usage_info = (VkImageViewUsageCreateInfo *)ivci->pNext;
   surface->info.flags = res->obj->vkflags;
   surface->info.usage = usage_info ? usage_info->usage : res->obj->vkusage;
   surface->info.width = surface->base.width;
   surface->info.height = surface->base.height;
   surface->info.layerCount = ivci->subresourceRange.layerCount;
   surface->info.format[0] = ivci->format;
   if (res->obj->dt) {
      struct kopper_displaytarget *cdt = res->obj->dt;
      if (zink_kopper_has_srgb(cdt))
         surface->info.format[1] = ivci->format == cdt->formats[0] ? cdt->formats[1] : cdt->formats[0];
   }
   surface->info_hash = _mesa_hash_data(&surface->info, sizeof(surface->info));
}

static struct zink_surface *
create_surface(struct pipe_context *pctx,
               struct pipe_resource *pres,
               const struct pipe_surface *templ,
               VkImageViewCreateInfo *ivci,
               bool actually)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);
   unsigned int level = templ->u.tex.level;

   struct zink_surface *surface = CALLOC_STRUCT(zink_surface);
   if (!surface)
      return NULL;

   surface->usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
   surface->usage_info.pNext = NULL;
   VkFormatFeatureFlags feats = res->linear ?
                                screen->format_props[templ->format].linearTilingFeatures :
                                screen->format_props[templ->format].optimalTilingFeatures;
   VkImageUsageFlags attachment = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT);
   surface->usage_info.usage = res->obj->vkusage & ~attachment;
   if (res->obj->modifier_aspect) {
      feats = res->obj->vkfeats;
      /* intersect format features for current modifier */
      for (unsigned i = 0; i < screen->modifier_props[templ->format].drmFormatModifierCount; i++) {
         if (res->obj->modifier == screen->modifier_props[templ->format].pDrmFormatModifierProperties[i].drmFormatModifier)
            feats &= screen->modifier_props[templ->format].pDrmFormatModifierProperties[i].drmFormatModifierTilingFeatures;
      }
   }
   if ((res->obj->vkusage & attachment) &&
       !(feats & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
      ivci->pNext = &surface->usage_info;
   }

   pipe_resource_reference(&surface->base.texture, pres);
   pipe_reference_init(&surface->base.reference, 1);
   surface->base.context = pctx;
   surface->base.format = templ->format;
   surface->base.width = u_minify(pres->width0, level);
   assert(surface->base.width);
   surface->base.height = u_minify(pres->height0, level);
   assert(surface->base.height);
   surface->base.nr_samples = templ->nr_samples;
   surface->base.u.tex.level = level;
   surface->base.u.tex.first_layer = templ->u.tex.first_layer;
   surface->base.u.tex.last_layer = templ->u.tex.last_layer;
   surface->obj = zink_resource(pres)->obj;

   init_surface_info(surface, res, ivci);

   if (!actually)
      return surface;
   assert(ivci->image);
   VkResult result = VKSCR(CreateImageView)(screen->dev, ivci, NULL,
                                            &surface->image_view);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateImageView failed (%s)", vk_Result_to_str(result));
      FREE(surface);
      return NULL;
   }

   return surface;
}

static uint32_t
hash_ivci(const void *key)
{
   return _mesa_hash_data((char*)key + offsetof(VkImageViewCreateInfo, flags), sizeof(VkImageViewCreateInfo) - offsetof(VkImageViewCreateInfo, flags));
}

static struct zink_surface *
do_create_surface(struct pipe_context *pctx, struct pipe_resource *pres, const struct pipe_surface *templ, VkImageViewCreateInfo *ivci, uint32_t hash, bool actually)
{
   /* create a new surface */
   struct zink_surface *surface = create_surface(pctx, pres, templ, ivci, actually);
   surface->base.nr_samples = 0;
   surface->hash = hash;
   surface->ivci = *ivci;
   return surface;
}

struct pipe_surface *
zink_get_surface(struct zink_context *ctx,
            struct pipe_resource *pres,
            const struct pipe_surface *templ,
            VkImageViewCreateInfo *ivci)
{
   struct zink_surface *surface = NULL;
   struct zink_resource *res = zink_resource(pres);
   uint32_t hash = hash_ivci(ivci);

   simple_mtx_lock(&res->surface_mtx);
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, hash, ivci);

   if (!entry) {
      /* create a new surface, but don't actually create the imageview if mutable isn't set */
      bool actually = pres->format == templ->format || (res->obj->vkflags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT);
      surface = do_create_surface(&ctx->base, pres, templ, ivci, hash, actually);
      entry = _mesa_hash_table_insert_pre_hashed(&res->surface_cache, hash, &surface->ivci, surface);
      if (!entry) {
         simple_mtx_unlock(&res->surface_mtx);
         return NULL;
      }

      surface = entry->data;
   } else {
      surface = entry->data;
      p_atomic_inc(&surface->base.reference.count);
   }
   simple_mtx_unlock(&res->surface_mtx);

   return &surface->base;
}

static struct pipe_surface *
wrap_surface(struct pipe_context *pctx, struct pipe_surface *psurf)
{
   struct zink_ctx_surface *csurf = CALLOC_STRUCT(zink_ctx_surface);
   csurf->base = *psurf;
   pipe_reference_init(&csurf->base.reference, 1);
   csurf->surf = (struct zink_surface*)psurf;
   csurf->base.context = pctx;

   return &csurf->base;
}

static struct pipe_surface *
zink_create_surface(struct pipe_context *pctx,
                    struct pipe_resource *pres,
                    const struct pipe_surface *templ)
{
   struct zink_resource *res = zink_resource(pres);
   bool is_array = templ->u.tex.last_layer != templ->u.tex.first_layer;
   enum pipe_texture_target target_2d[] = {PIPE_TEXTURE_2D, PIPE_TEXTURE_2D_ARRAY};
   if (!res->obj->dt && pres->format != templ->format)
      /* mutable not set by default */
      zink_resource_object_init_mutable(zink_context(pctx), res);

   VkImageViewCreateInfo ivci = create_ivci(zink_screen(pctx->screen), res, templ,
                                            pres->target == PIPE_TEXTURE_3D ? target_2d[is_array] : pres->target);

   struct pipe_surface *psurf = NULL;
   if (res->obj->dt) {
      /* don't cache swapchain surfaces. that's weird. */
      struct zink_surface *surface = do_create_surface(pctx, pres, templ, &ivci, 0, false);
      if (surface) {
         surface->is_swapchain = true;
         psurf = &surface->base;
      }
   } else
      psurf = zink_get_surface(zink_context(pctx), pres, templ, &ivci);
   if (!psurf)
      return NULL;

   struct zink_ctx_surface *csurf = (struct zink_ctx_surface*)wrap_surface(pctx, psurf);

   if (templ->nr_samples) {
      /* transient fb attachment: not cached */
      struct pipe_resource rtempl = *pres;
      rtempl.nr_samples = templ->nr_samples;
      rtempl.bind |= ZINK_BIND_TRANSIENT;
      struct zink_resource *transient = zink_resource(pctx->screen->resource_create(pctx->screen, &rtempl));
      if (!transient)
         return NULL;
      ivci.image = transient->obj->image;
      csurf->transient = (struct zink_ctx_surface*)wrap_surface(pctx, (struct pipe_surface*)create_surface(pctx, &transient->base.b, templ, &ivci, true));
      if (!csurf->transient) {
         pipe_resource_reference((struct pipe_resource**)&transient, NULL);
         pipe_surface_release(pctx, &psurf);
         return NULL;
      }
      pipe_resource_reference((struct pipe_resource**)&transient, NULL);
   }

   return &csurf->base;
}

void
zink_destroy_surface(struct zink_screen *screen, struct pipe_surface *psurface)
{
   struct zink_surface *surface = zink_surface(psurface);
   struct zink_resource *res = zink_resource(psurface->texture);
   if (!psurface->nr_samples && !surface->is_swapchain) {
      simple_mtx_lock(&res->surface_mtx);
      if (psurface->reference.count) {
         /* got a cache hit during deletion */
         simple_mtx_unlock(&res->surface_mtx);
         return;
      }
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(&res->surface_cache, surface->hash, &surface->ivci);
      assert(he);
      assert(he->data == surface);
      _mesa_hash_table_remove(&res->surface_cache, he);
      simple_mtx_unlock(&res->surface_mtx);
   }
   if (surface->simage_view)
      VKSCR(DestroyImageView)(screen->dev, surface->simage_view, NULL);
   if (surface->is_swapchain) {
      for (unsigned i = 0; i < surface->old_swapchain_size; i++)
         VKSCR(DestroyImageView)(screen->dev, surface->old_swapchain[i], NULL);
      for (unsigned i = 0; i < surface->swapchain_size; i++)
         VKSCR(DestroyImageView)(screen->dev, surface->swapchain[i], NULL);
      free(surface->swapchain);
   } else
      VKSCR(DestroyImageView)(screen->dev, surface->image_view, NULL);
   pipe_resource_reference(&psurface->texture, NULL);
   FREE(surface);
}

static void
zink_surface_destroy(struct pipe_context *pctx,
                     struct pipe_surface *psurface)
{
   struct zink_ctx_surface *csurf = (struct zink_ctx_surface *)psurface;
   zink_surface_reference(zink_screen(pctx->screen), &csurf->surf, NULL);
   pipe_surface_release(pctx, (struct pipe_surface**)&csurf->transient);
   FREE(csurf);
}

bool
zink_rebind_surface(struct zink_context *ctx, struct pipe_surface **psurface)
{
   struct zink_surface *surface = zink_surface(*psurface);
   struct zink_resource *res = zink_resource((*psurface)->texture);
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (surface->simage_view)
      return false;
   assert(!res->obj->dt);
   VkImageViewCreateInfo ivci = surface->ivci;
   ivci.image = res->obj->image;
   uint32_t hash = hash_ivci(&ivci);

   simple_mtx_lock(&res->surface_mtx);
   struct hash_entry *new_entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, hash, &ivci);
   if (zink_batch_usage_exists(surface->batch_uses))
      zink_batch_reference_surface(&ctx->batch, surface);
   if (new_entry) {
      /* reuse existing surface; old one will be cleaned up naturally */
      struct zink_surface *new_surface = new_entry->data;
      simple_mtx_unlock(&res->surface_mtx);
      zink_batch_usage_set(&new_surface->batch_uses, ctx->batch.state);
      zink_surface_reference(screen, (struct zink_surface**)psurface, new_surface);
      return true;
   }
   struct hash_entry *entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, surface->hash, &surface->ivci);
   assert(entry);
   _mesa_hash_table_remove(&res->surface_cache, entry);
   VkImageView image_view;
   VkResult result = VKSCR(CreateImageView)(screen->dev, &ivci, NULL, &image_view);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: failed to create new imageview (%s)", vk_Result_to_str(result));
      simple_mtx_unlock(&res->surface_mtx);
      return false;
   }
   surface->hash = hash;
   surface->ivci = ivci;
   entry = _mesa_hash_table_insert_pre_hashed(&res->surface_cache, surface->hash, &surface->ivci, surface);
   assert(entry);
   surface->simage_view = surface->image_view;
   surface->image_view = image_view;
   surface->obj = zink_resource(surface->base.texture)->obj;
   /* update for imageless fb */
   surface->info.flags = res->obj->vkflags;
   surface->info.usage = res->obj->vkusage;
   surface->info_hash = _mesa_hash_data(&surface->info, sizeof(surface->info));
   zink_batch_usage_set(&surface->batch_uses, ctx->batch.state);
   simple_mtx_unlock(&res->surface_mtx);
   return true;
}

struct pipe_surface *
zink_surface_create_null(struct zink_context *ctx, enum pipe_texture_target target, unsigned width, unsigned height, unsigned samples)
{
   struct pipe_surface surf_templ = {0};

   struct pipe_resource *pres;
   struct pipe_resource templ = {0};
   templ.width0 = width;
   templ.height0 = height;
   templ.depth0 = 1;
   templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
   templ.target = target;
   templ.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_SAMPLER_VIEW;
   if (samples < 2)
      templ.bind |= PIPE_BIND_SHADER_IMAGE;
   templ.nr_samples = samples;

   pres = ctx->base.screen->resource_create(ctx->base.screen, &templ);
   if (!pres)
      return NULL;

   surf_templ.format = PIPE_FORMAT_R8G8B8A8_UNORM;
   surf_templ.nr_samples = 0;
   struct pipe_surface *psurf = ctx->base.create_surface(&ctx->base, pres, &surf_templ);
   pipe_resource_reference(&pres, NULL);
   return psurf;
}

void
zink_context_surface_init(struct pipe_context *context)
{
   context->create_surface = zink_create_surface;
   context->surface_destroy = zink_surface_destroy;
}

void
zink_surface_swapchain_update(struct zink_context *ctx, struct zink_surface *surface)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_resource *res = zink_resource(surface->base.texture);
   struct kopper_displaytarget *cdt = res->obj->dt;
   if (!cdt)
      return; //dead swapchain
   if (res->obj->dt != surface->dt) {
      /* new swapchain: clear out previous old_swapchain and move current swapchain there */
      for (unsigned i = 0; i < surface->old_swapchain_size; i++)
         util_dynarray_append(&ctx->batch.state->dead_swapchains, VkImageView, surface->old_swapchain[i]);
      free(surface->old_swapchain);
      surface->old_swapchain = surface->swapchain;
      surface->old_swapchain_size = surface->swapchain_size;
      surface->swapchain_size = cdt->swapchain->num_images;
      surface->swapchain = calloc(surface->swapchain_size, sizeof(VkImageView));
      surface->base.width = res->base.b.width0;
      surface->base.height = res->base.b.height0;
      init_surface_info(surface, res, &surface->ivci);
   }
   if (!surface->swapchain[res->obj->dt_idx]) {
      assert(res->obj->image && cdt->swapchain->images[res->obj->dt_idx].image == res->obj->image);
      surface->ivci.image = res->obj->image;
      assert(surface->ivci.image);
      VKSCR(CreateImageView)(screen->dev, &surface->ivci, NULL, &surface->swapchain[res->obj->dt_idx]);
   }
   surface->image_view = surface->swapchain[res->obj->dt_idx];
}
