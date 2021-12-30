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

 #ifndef ZINK_SURFACE_H
 #define ZINK_SURFACE_H

#include "pipe/p_state.h"
#include "zink_batch.h"
#include <vulkan/vulkan.h>

struct pipe_context;

struct zink_surface_info {
   VkImageCreateFlags flags;
   VkImageUsageFlags usage;
   uint32_t width;
   uint32_t height;
   uint32_t layerCount;
   VkFormat format;
};

struct zink_surface {
   struct pipe_surface base;
   VkImageViewCreateInfo ivci;
   struct zink_surface_info info; //TODO: union with fb refs
   uint32_t info_hash;
   VkImageView image_view;
   VkImageView simage_view;//old iview after storage replacement/rebind
   void *obj; //backing resource object
   uint32_t hash;
   struct zink_batch_usage *batch_uses;
   struct util_dynarray framebuffer_refs;
   struct zink_descriptor_refs desc_set_refs;
};

/* wrapper object that preserves the gallium expectation of having
 * pipe_surface::context match the context used to create the surface
 */
struct zink_ctx_surface {
   struct pipe_surface base;
   struct zink_surface *surf;
   struct zink_ctx_surface *transient; //zink_ctx_surface
   /* TODO: need replicate EXT */
   bool transient_init;
};

/* use this cast for framebuffer surfaces */
static inline struct zink_surface *
zink_csurface(struct pipe_surface *psurface)
{
   return psurface ? ((struct zink_ctx_surface *)psurface)->surf : NULL;
}

/* use this cast for checking transient framebuffer surfaces */
static inline struct zink_surface *
zink_transient_surface(struct pipe_surface *psurface)
{
   return psurface ? ((struct zink_ctx_surface *)psurface)->transient ? ((struct zink_ctx_surface *)psurface)->transient->surf : NULL : NULL;
}

/* use this cast for internal surfaces */
static inline struct zink_surface *
zink_surface(struct pipe_surface *psurface)
{
   return (struct zink_surface *)psurface;
}

void
zink_destroy_surface(struct zink_screen *screen, struct pipe_surface *psurface);

static inline void
zink_surface_reference(struct zink_screen *screen, struct zink_surface **dst, struct zink_surface *src)
{
   struct zink_surface *old_dst = *dst;

   if (pipe_reference_described(old_dst ? &old_dst->base.reference : NULL,
                                src ? &src->base.reference : NULL,
                                (debug_reference_descriptor)
                                debug_describe_surface))
      zink_destroy_surface(screen, &old_dst->base);
   *dst = src;
}

void
zink_context_surface_init(struct pipe_context *context);

VkImageViewCreateInfo
create_ivci(struct zink_screen *screen,
            struct zink_resource *res,
            const struct pipe_surface *templ,
            enum pipe_texture_target target);

struct pipe_surface *
zink_get_surface(struct zink_context *ctx,
            struct pipe_resource *pres,
            const struct pipe_surface *templ,
            VkImageViewCreateInfo *ivci);

static inline VkImageViewType
zink_surface_clamp_viewtype(VkImageViewType viewType, unsigned first_layer, unsigned last_layer, unsigned array_size)
{
   unsigned layerCount = 1 + last_layer - first_layer;
   if (viewType == VK_IMAGE_VIEW_TYPE_CUBE || viewType == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY) {
      if (first_layer == last_layer)
         return VK_IMAGE_VIEW_TYPE_2D;
      if (layerCount % 6 != 0 && (first_layer || layerCount != array_size))
         return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
   }
   return viewType;
}

bool
zink_rebind_surface(struct zink_context *ctx, struct pipe_surface **psurface);

static inline bool
zink_rebind_ctx_surface(struct zink_context *ctx, struct pipe_surface **psurface)
{
   struct zink_ctx_surface *csurf = (struct zink_ctx_surface*)*psurface;
   return zink_rebind_surface(ctx, (struct pipe_surface**)&csurf->surf);
}

struct pipe_surface *
zink_surface_create_null(struct zink_context *ctx, enum pipe_texture_target target, unsigned width, unsigned height, unsigned samples);
#endif
