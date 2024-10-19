/*
 * Copyright © 2024 Collabora Ltd.
 *
 * Derived from tu_cmd_buffer.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"
#include "panvk_event.h"

#include "util/u_dynarray.h"

static void
panvk_add_set_event_operation(struct panvk_cmd_buffer *cmdbuf,
                              struct panvk_event *event,
                              enum panvk_cmd_event_op_type type)
{
   struct panvk_cmd_event_op op = {
      .type = type,
      .event = event,
   };

   if (cmdbuf->cur_batch == NULL) {
      /* No open batch, let's create a new one so this operation happens in
       * the right order.
       */
      panvk_per_arch(cmd_open_batch)(cmdbuf);
      util_dynarray_append(&cmdbuf->cur_batch->event_ops,
                           struct panvk_cmd_event_op, op);
      panvk_per_arch(cmd_close_batch)(cmdbuf);
   } else {
      /* Let's close the current batch so the operation executes before any
       * future commands.
       */
      util_dynarray_append(&cmdbuf->cur_batch->event_ops,
                           struct panvk_cmd_event_op, op);
      panvk_per_arch(cmd_close_batch)(cmdbuf);
      panvk_per_arch(cmd_preload_fb_after_batch_split)(cmdbuf);
      panvk_per_arch(cmd_open_batch)(cmdbuf);
   }
}

static void
panvk_add_wait_event_operation(struct panvk_cmd_buffer *cmdbuf,
                               struct panvk_event *event)
{
   struct panvk_cmd_event_op op = {
      .type = PANVK_EVENT_OP_WAIT,
      .event = event,
   };

   if (cmdbuf->cur_batch == NULL) {
      /* No open batch, let's create a new one and have it wait for this event. */
      panvk_per_arch(cmd_open_batch)(cmdbuf);
      util_dynarray_append(&cmdbuf->cur_batch->event_ops,
                           struct panvk_cmd_event_op, op);
   } else {
      /* Let's close the current batch so any future commands wait on the
       * event signal operation.
       */
      if (cmdbuf->cur_batch->frag_jc.first_job ||
          cmdbuf->cur_batch->vtc_jc.first_job) {
         panvk_per_arch(cmd_close_batch)(cmdbuf);
         panvk_per_arch(cmd_preload_fb_after_batch_split)(cmdbuf);
         panvk_per_arch(cmd_open_batch)(cmdbuf);
      }
      util_dynarray_append(&cmdbuf->cur_batch->event_ops,
                           struct panvk_cmd_event_op, op);
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdSetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                             const VkDependencyInfo *pDependencyInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);

   /* vkCmdSetEvent cannot be called inside a render pass */
   assert(cmdbuf->vk.render_pass == NULL);

   panvk_add_set_event_operation(cmdbuf, event, PANVK_EVENT_OP_SET);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResetEvent2)(VkCommandBuffer commandBuffer, VkEvent _event,
                               VkPipelineStageFlags2 stageMask)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);
   VK_FROM_HANDLE(panvk_event, event, _event);

   /* vkCmdResetEvent cannot be called inside a render pass */
   assert(cmdbuf->vk.render_pass == NULL);

   panvk_add_set_event_operation(cmdbuf, event, PANVK_EVENT_OP_RESET);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdWaitEvents2)(VkCommandBuffer commandBuffer,
                               uint32_t eventCount, const VkEvent *pEvents,
                               const VkDependencyInfo *pDependencyInfos)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(eventCount > 0);

   for (uint32_t i = 0; i < eventCount; i++) {
      VK_FROM_HANDLE(panvk_event, event, pEvents[i]);
      panvk_add_wait_event_operation(cmdbuf, event);
   }
}
