/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "util/u_inlines.h"
#include "util/u_hash_table.h"
#include "util/u_memory.h"
#include "util/simple_list.h"

#include "tr_screen.h"
#include "tr_context.h"
#include "tr_texture.h"


struct pipe_surface *
trace_surf_create(struct trace_context *tr_ctx,
                  struct pipe_resource *res,
                  struct pipe_surface *surface)
{
   struct trace_surface *tr_surf;

   if (!surface)
      goto error;

   assert(surface->texture == res);

   tr_surf = CALLOC_STRUCT(trace_surface);
   if (!tr_surf)
      goto error;

   memcpy(&tr_surf->base, surface, sizeof(struct pipe_surface));
   tr_surf->base.context = &tr_ctx->base;

   pipe_reference_init(&tr_surf->base.reference, 1);
   tr_surf->base.texture = NULL;
   pipe_resource_reference(&tr_surf->base.texture, res);
   tr_surf->surface = surface;

   return &tr_surf->base;

error:
   pipe_surface_reference(&surface, NULL);
   return NULL;
}


void
trace_surf_destroy(struct trace_surface *tr_surf)
{
   trace_context_check(tr_surf->base.context);
   pipe_resource_reference(&tr_surf->base.texture, NULL);
   pipe_surface_reference(&tr_surf->surface, NULL);
   FREE(tr_surf);
}


struct pipe_transfer *
trace_transfer_create(struct trace_context *tr_ctx,
		      struct pipe_resource *res,
		      struct pipe_transfer *transfer)
{
   struct trace_transfer *tr_trans;

   if (!transfer)
      goto error;

   tr_trans = CALLOC_STRUCT(trace_transfer);
   if (!tr_trans)
      goto error;

   memcpy(&tr_trans->base, transfer, sizeof(struct pipe_transfer));

   tr_trans->base.resource = NULL;
   tr_trans->transfer = transfer;

   pipe_resource_reference(&tr_trans->base.resource, res);
   assert(tr_trans->base.resource == res);

   return &tr_trans->base;

error:
   tr_ctx->pipe->transfer_unmap(tr_ctx->pipe, transfer);
   return NULL;
}


void
trace_transfer_destroy(struct trace_context *tr_context,
                       struct trace_transfer *tr_trans)
{
   pipe_resource_reference(&tr_trans->base.resource, NULL);
   FREE(tr_trans);
}

