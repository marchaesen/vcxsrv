/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 ***************************************************************************/

#ifndef SWR_RESOURCE_H
#define SWR_RESOURCE_H

#include "memory/SurfaceState.h"
#include "pipe/p_state.h"
#include "api.h"

struct sw_displaytarget;

enum swr_resource_status {
   SWR_RESOURCE_UNUSED = 0x0,
   SWR_RESOURCE_READ = 0x1,
   SWR_RESOURCE_WRITE = 0x2,
};

struct swr_resource {
   struct pipe_resource base;

   bool has_depth;
   bool has_stencil;

   SWR_SURFACE_STATE swr;
   SWR_SURFACE_STATE secondary; /* for faking depth/stencil merged formats */

   struct sw_displaytarget *display_target;

   /* If resource is multisample, then this points to a alternate resource
    * containing the resolved multisample surface, otherwise null */
   struct pipe_resource *resolve_target;

   size_t mip_offsets[PIPE_MAX_TEXTURE_LEVELS];
   size_t secondary_mip_offsets[PIPE_MAX_TEXTURE_LEVELS];

   enum swr_resource_status status;

   /* last pipe that used (validated) this resource */
   struct pipe_context *curr_pipe;
};


static INLINE struct swr_resource *
swr_resource(struct pipe_resource *resource)
{
   return (struct swr_resource *)resource;
}

static INLINE bool
swr_resource_is_texture(const struct pipe_resource *resource)
{
   switch (resource->target) {
   case PIPE_BUFFER:
      return false;
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_3D:
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return true;
   default:
      assert(0);
      return false;
   }
}


static INLINE uint8_t *
swr_resource_data(struct pipe_resource *resource)
{
   struct swr_resource *swr_r = swr_resource(resource);

   assert(!swr_resource_is_texture(resource));

   return (uint8_t*)(swr_r->swr.xpBaseAddress);
}


void swr_invalidate_render_target(struct pipe_context *pipe,
                                  uint32_t attachment,
                                  uint16_t width, uint16_t height);

void swr_store_render_target(struct pipe_context *pipe,
                             uint32_t attachment,
                             enum SWR_TILE_STATE post_tile_state);

void swr_store_dirty_resource(struct pipe_context *pipe,
                              struct pipe_resource *resource,
                              enum SWR_TILE_STATE post_tile_state);

void swr_update_resource_status(struct pipe_context *,
                                const struct pipe_draw_info *);

/*
 * Functions to indicate a resource's in-use status.
 */
static INLINE enum
swr_resource_status & operator|=(enum swr_resource_status & a,
                                 enum swr_resource_status  b) {
   return (enum swr_resource_status &)((int&)a |= (int)b);
}

static INLINE void
swr_resource_read(struct pipe_resource *resource)
{
   swr_resource(resource)->status |= SWR_RESOURCE_READ;
}

static INLINE void
swr_resource_write(struct pipe_resource *resource)
{
   swr_resource(resource)->status |= SWR_RESOURCE_WRITE;
}

static INLINE void
swr_resource_unused(struct pipe_resource *resource)
{
   swr_resource(resource)->status = SWR_RESOURCE_UNUSED;
}

#endif
