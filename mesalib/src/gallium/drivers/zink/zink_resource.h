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

#ifndef ZINK_RESOURCE_H
#define ZINK_RESOURCE_H

struct pipe_screen;
struct sw_displaytarget;
struct zink_batch;

#include "util/u_transfer.h"

#include <vulkan/vulkan.h>

#define ZINK_RESOURCE_ACCESS_READ 1
#define ZINK_RESOURCE_ACCESS_WRITE 16

struct zink_resource {
   struct pipe_resource base;

   enum pipe_format internal_format:16;

   union {
      VkBuffer buffer;
      struct {
         VkFormat format;
         VkImage image;
         VkImageLayout layout;
         VkImageAspectFlags aspect;
         bool optimal_tiling;
      };
   };
   VkDeviceMemory mem;
   VkDeviceSize offset, size;

   struct sw_displaytarget *dt;
   unsigned dt_stride;

   /* this has to be atomic for fence access, so we can't use a bitmask and make everything neat */
   uint8_t batch_uses[4];
   bool needs_xfb_barrier;
};

struct zink_transfer {
   struct pipe_transfer base;
   struct pipe_resource *staging_res;
};

static inline struct zink_resource *
zink_resource(struct pipe_resource *r)
{
   return (struct zink_resource *)r;
}

void
zink_screen_resource_init(struct pipe_screen *pscreen);

void
zink_context_resource_init(struct pipe_context *pctx);

void
zink_get_depth_stencil_resources(struct pipe_resource *res,
                                 struct zink_resource **out_z,
                                 struct zink_resource **out_s);

void
zink_resource_setup_transfer_layouts(struct zink_batch *batch, struct zink_resource *src, struct zink_resource *dst);
#endif
