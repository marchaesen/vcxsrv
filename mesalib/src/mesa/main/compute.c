/*
 * Copyright © 2014 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "glheader.h"
#include "compute.h"
#include "context.h"
#include "api_validate.h"

void GLAPIENTRY
_mesa_DispatchCompute(GLuint num_groups_x,
                      GLuint num_groups_y,
                      GLuint num_groups_z)
{
   GET_CURRENT_CONTEXT(ctx);
   const GLuint num_groups[3] = { num_groups_x, num_groups_y, num_groups_z };

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glDispatchCompute(%d, %d, %d)\n",
                  num_groups_x, num_groups_y, num_groups_z);

   if (!_mesa_validate_DispatchCompute(ctx, num_groups))
      return;

   if (num_groups_x == 0u || num_groups_y == 0u || num_groups_z == 0u)
       return;

   ctx->Driver.DispatchCompute(ctx, num_groups);
}

extern void GLAPIENTRY
_mesa_DispatchComputeIndirect(GLintptr indirect)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glDispatchComputeIndirect(%ld)\n", (long) indirect);

   if (!_mesa_validate_DispatchComputeIndirect(ctx, indirect))
      return;

   ctx->Driver.DispatchComputeIndirect(ctx, indirect);
}

void GLAPIENTRY
_mesa_DispatchComputeGroupSizeARB(GLuint num_groups_x, GLuint num_groups_y,
                                  GLuint num_groups_z, GLuint group_size_x,
                                  GLuint group_size_y, GLuint group_size_z)
{
   GET_CURRENT_CONTEXT(ctx);
   const GLuint num_groups[3] = { num_groups_x, num_groups_y, num_groups_z };
   const GLuint group_size[3] = { group_size_x, group_size_y, group_size_z };

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx,
                  "glDispatchComputeGroupSizeARB(%d, %d, %d, %d, %d, %d)\n",
                  num_groups_x, num_groups_y, num_groups_z,
                  group_size_x, group_size_y, group_size_z);

   if (!_mesa_validate_DispatchComputeGroupSizeARB(ctx, num_groups,
                                                   group_size))
      return;

   if (num_groups_x == 0u || num_groups_y == 0u || num_groups_z == 0u)
       return;

   ctx->Driver.DispatchComputeGroupSize(ctx, num_groups, group_size);
}
