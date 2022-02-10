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
      ivci.viewType = res->need_2D_zs ? VK_IMAGE_VIEW_TYPE_2D : VK_IMAGE_VIEW_TYPE_1D;
      break;

   case PIPE_TEXTURE_1D_ARRAY:
      ivci.viewType = res->need_2D_zs ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_1D_ARRAY;
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
   surface->info.format = ivci->format;
   surface->info_hash = _mesa_hash_data(&surface->info, sizeof(surface->info));
}

static struct zink_surface *
create_surface(struct pipe_context *pctx,
               struct pipe_resource *pres,
               const struct pipe_surface *templ,
               VkImageViewCreateInfo *ivci)
{
   struct zink_screen *screen = zink_screen(pctx->screen);
   struct zink_resource *res = zink_resource(pres);
   unsigned int level = templ->u.tex.level;

   struct zink_surface *surface = CALLOC_STRUCT(zink_surface);
   if (!surface)
      return NULL;

   VkImageViewUsageCreateInfo usage_info;
   usage_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO;
   usage_info.pNext = NULL;
   VkFormatFeatureFlags feats = res->optimal_tiling ?
                                screen->format_props[templ->format].optimalTilingFeatures :
                                screen->format_props[templ->format].linearTilingFeatures;
   VkImageUsageFlags attachment = (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
   usage_info.usage = res->obj->vkusage & ~attachment;
   if ((res->obj->vkusage & attachment) &&
       !(feats & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT | VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
      ivci->pNext = &usage_info;
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
   util_dynarray_init(&surface->framebuffer_refs, NULL);
   util_dynarray_init(&surface->desc_set_refs.refs, NULL);

   init_surface_info(surface, res, ivci);

   if (VKSCR(CreateImageView)(screen->dev, ivci, NULL,
                         &surface->image_view) != VK_SUCCESS) {
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
      /* create a new surface */
      surface = create_surface(&ctx->base, pres, templ, ivci);
      surface->base.nr_samples = 0;
      surface->hash = hash;
      surface->ivci = *ivci;
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

   VkImageViewCreateInfo ivci = create_ivci(zink_screen(pctx->screen),
                                            zink_resource(pres), templ, pres->target);
   if (pres->target == PIPE_TEXTURE_3D)
      ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;

   struct pipe_surface *psurf = zink_get_surface(zink_context(pctx), pres, templ, &ivci);
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
      csurf->transient = (struct zink_ctx_surface*)wrap_surface(pctx, (struct pipe_surface*)create_surface(pctx, &transient->base.b, templ, &ivci));
      if (!csurf->transient) {
         pipe_resource_reference((struct pipe_resource**)&transient, NULL);
         pipe_surface_release(pctx, &psurf);
         return NULL;
      }
      pipe_resource_reference((struct pipe_resource**)&transient, NULL);
   }

   return &csurf->base;
}

/* framebuffers are owned by their surfaces, so each time a surface that's part of a cached fb
 * is destroyed, it has to unref all the framebuffers it's attached to in order to avoid leaking
 * all the framebuffers
 *
 * surfaces are always batch-tracked, so it is impossible for a framebuffer to be destroyed
 * while it is in use
 */
static void
surface_clear_fb_refs(struct zink_screen *screen, struct pipe_surface *psurface)
{
   struct zink_surface *surface = zink_surface(psurface);
   util_dynarray_foreach(&surface->framebuffer_refs, struct zink_framebuffer*, fb_ref) {
      struct zink_framebuffer *fb = *fb_ref;
      for (unsigned i = 0; i < fb->state.num_attachments; i++) {
         if (fb->surfaces[i] == psurface) {
            simple_mtx_lock(&screen->framebuffer_mtx);
            fb->surfaces[i] = NULL;
            _mesa_hash_table_remove_key(&screen->framebuffer_cache, &fb->state);
            zink_framebuffer_reference(screen, &fb, NULL);
            simple_mtx_unlock(&screen->framebuffer_mtx);
            break;
         }
      }
   }
   util_dynarray_fini(&surface->framebuffer_refs);
}

void
zink_destroy_surface(struct zink_screen *screen, struct pipe_surface *psurface)
{
   struct zink_surface *surface = zink_surface(psurface);
   struct zink_resource *res = zink_resource(psurface->texture);
   if (!psurface->nr_samples) {
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
   if (!screen->info.have_KHR_imageless_framebuffer)
      surface_clear_fb_refs(screen, psurface);
   zink_descriptor_set_refs_clear(&surface->desc_set_refs, surface);
   util_dynarray_fini(&surface->framebuffer_refs);
   pipe_resource_reference(&psurface->texture, NULL);
   if (surface->simage_view)
      VKSCR(DestroyImageView)(screen->dev, surface->simage_view, NULL);
   VKSCR(DestroyImageView)(screen->dev, surface->image_view, NULL);
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
   VkImageViewCreateInfo ivci = create_ivci(screen,
                                            zink_resource((*psurface)->texture), (*psurface), surface->base.texture->target);
   uint32_t hash = hash_ivci(&ivci);

   simple_mtx_lock(&res->surface_mtx);
   struct hash_entry *new_entry = _mesa_hash_table_search_pre_hashed(&res->surface_cache, hash, &ivci);
   if (zink_batch_usage_exists(surface->batch_uses))
      zink_batch_reference_surface(&ctx->batch, surface);
   surface_clear_fb_refs(screen, *psurface);
   zink_descriptor_set_refs_clear(&surface->desc_set_refs, surface);
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
   if (VKSCR(CreateImageView)(screen->dev, &ivci, NULL, &image_view) != VK_SUCCESS) {
      debug_printf("zink: failed to create new imageview");
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
   templ.format = PIPE_FORMAT_R8_UINT;
   templ.target = target;
   templ.bind = PIPE_BIND_RENDER_TARGET;
   templ.nr_samples = samples;

   pres = ctx->base.screen->resource_create(ctx->base.screen, &templ);
   if (!pres)
      return NULL;

   surf_templ.format = PIPE_FORMAT_R8_UINT;
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
