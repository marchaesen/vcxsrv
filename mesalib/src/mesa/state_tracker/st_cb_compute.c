/**************************************************************************
 *
 * Copyright 2016 Samuel Pitoiset
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

#include "main/state.h"
#include "st_atom.h"
#include "st_context.h"
#include "st_cb_bitmap.h"
#include "st_cb_bufferobjects.h"
#include "st_cb_compute.h"

#include "pipe/p_context.h"

static void st_dispatch_compute_common(struct gl_context *ctx,
                                       const GLuint *num_groups,
                                       const GLuint *group_size,
                                       struct pipe_resource *indirect,
                                       GLintptr indirect_offset)
{
   struct gl_shader_program *prog =
      ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];
   struct st_context *st = st_context(ctx);
   struct pipe_context *pipe = st->pipe;
   struct pipe_grid_info info = { 0 };

   st_flush_bitmap_cache(st);
   st_invalidate_readpix_cache(st);

   if (ctx->NewState)
      _mesa_update_state(ctx);

   if ((st->dirty | ctx->NewDriverState) & ST_PIPELINE_COMPUTE_STATE_MASK ||
       st->compute_shader_may_be_dirty)
      st_validate_state(st, ST_PIPELINE_COMPUTE);

   for (unsigned i = 0; i < 3; i++) {
      info.block[i] = group_size ? group_size[i] : prog->Comp.LocalSize[i];
      info.grid[i]  = num_groups ? num_groups[i] : 0;
   }

   if (indirect) {
      info.indirect = indirect;
      info.indirect_offset = indirect_offset;
   }

   pipe->launch_grid(pipe, &info);
}

static void st_dispatch_compute(struct gl_context *ctx,
                                const GLuint *num_groups)
{
   st_dispatch_compute_common(ctx, num_groups, NULL, NULL, 0);
}

static void st_dispatch_compute_indirect(struct gl_context *ctx,
                                         GLintptr indirect_offset)
{
   struct gl_buffer_object *indirect_buffer = ctx->DispatchIndirectBuffer;
   struct pipe_resource *indirect = st_buffer_object(indirect_buffer)->buffer;

   st_dispatch_compute_common(ctx, NULL, NULL, indirect, indirect_offset);
}

static void st_dispatch_compute_group_size(struct gl_context *ctx,
                                           const GLuint *num_groups,
                                           const GLuint *group_size)
{
   st_dispatch_compute_common(ctx, num_groups, group_size, NULL, 0);
}

void st_init_compute_functions(struct dd_function_table *functions)
{
   functions->DispatchCompute = st_dispatch_compute;
   functions->DispatchComputeIndirect = st_dispatch_compute_indirect;
   functions->DispatchComputeGroupSize = st_dispatch_compute_group_size;
}
