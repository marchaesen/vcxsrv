/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
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
 */

#include "c99_alloca.h"

#include "main/glthread_marshal.h"
#include "main/dispatch.h"

uint32_t
_mesa_unmarshal_CallList(struct gl_context *ctx, const struct marshal_cmd_CallList *cmd, const uint64_t *last)
{
   const GLuint list = cmd->list;
   uint64_t *ptr = (uint64_t *) cmd;
   ptr += cmd->cmd_base.cmd_size;

   if (ptr < last) {
      const struct marshal_cmd_base *next =
         (const struct marshal_cmd_base *)ptr;

      /* If the 'next' is also a DISPATCH_CMD_CallList, we transform 'cmd' and 'next' in a CALL_CallLists.
       * If the following commands are also CallList they're including in the CallLists we're building.
       */
      if (next->cmd_id == DISPATCH_CMD_CallList) {
         const int max_list_count = 2048;
         struct marshal_cmd_CallList *next_callist = (struct marshal_cmd_CallList *) next;
         uint32_t *lists = alloca(max_list_count * sizeof(uint32_t));

         lists[0] = cmd->list;
         lists[1] = next_callist->list;

         int count = 2;

         ptr += next->cmd_size;
         while (ptr < last && count < max_list_count) {
            next = (const struct marshal_cmd_base *)ptr;
            if (next->cmd_id == DISPATCH_CMD_CallList) {
               next_callist = (struct marshal_cmd_CallList *) next;
               lists[count++] = next_callist->list;
               ptr += next->cmd_size;
            } else {
               break;
            }
         }

         CALL_CallLists(ctx->CurrentServerDispatch, (count, GL_UNSIGNED_INT, lists));

         return (uint32_t) (ptr - (uint64_t*)cmd);
      }
   }

   CALL_CallList(ctx->CurrentServerDispatch, (list));
   return cmd->cmd_base.cmd_size;
}

void GLAPIENTRY
_mesa_marshal_CallList(GLuint list)
{
   GET_CURRENT_CONTEXT(ctx);
   int cmd_size = sizeof(struct marshal_cmd_CallList);
   struct marshal_cmd_CallList *cmd;
   cmd = _mesa_glthread_allocate_command(ctx, DISPATCH_CMD_CallList, cmd_size);
   cmd->list = list;

   _mesa_glthread_CallList(ctx, list);
}
