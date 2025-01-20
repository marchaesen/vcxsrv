/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_queue.h"

#include "tu_cmd_buffer.h"
#include "tu_dynamic_rendering.h"
#include "tu_knl.h"
#include "tu_device.h"

#include "vk_util.h"

static int
tu_get_submitqueue_priority(const struct tu_physical_device *pdevice,
                            VkQueueGlobalPriorityKHR global_priority,
                            bool global_priority_query)
{
   if (global_priority_query) {
      VkQueueFamilyGlobalPriorityPropertiesKHR props;
      tu_physical_device_get_global_priority_properties(pdevice, &props);

      bool valid = false;
      for (uint32_t i = 0; i < props.priorityCount; i++) {
         if (props.priorities[i] == global_priority) {
            valid = true;
            break;
         }
      }

      if (!valid)
         return -1;
   }

   /* Valid values are from 0 to (pdevice->submitqueue_priority_count - 1),
    * with 0 being the highest priority.  This matches what freedreno does.
    */
   int priority;
   if (global_priority == VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR)
      priority = pdevice->submitqueue_priority_count / 2;
   else if (global_priority < VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR)
      priority = pdevice->submitqueue_priority_count - 1;
   else
      priority = 0;

   return priority;
}

static void
submit_add_entries(struct tu_device *dev, void *submit,
                   struct util_dynarray *dump_cmds,
                   struct tu_cs_entry *entries, unsigned num_entries)
{
   tu_submit_add_entries(dev, submit, entries, num_entries);
   if (FD_RD_DUMP(ENABLE)) {
      util_dynarray_append_array(dump_cmds, struct tu_cs_entry, entries,
                                 num_entries);
   }
}

static VkResult
queue_submit(struct vk_queue *_queue, struct vk_queue_submit *vk_submit)
{
   struct tu_queue *queue = list_entry(_queue, struct tu_queue, vk);
   struct tu_device *device = queue->device;
   bool u_trace_enabled = u_trace_should_process(&queue->device->trace_context);
   struct util_dynarray dump_cmds;

   util_dynarray_init(&dump_cmds, NULL);

   uint32_t perf_pass_index =
      device->perfcntrs_pass_cs_entries ? vk_submit->perf_pass_index : ~0;

   if (TU_DEBUG(LOG_SKIP_GMEM_OPS))
      tu_dbg_log_gmem_load_store_skips(device);

   pthread_mutex_lock(&device->submit_mutex);

   struct tu_cmd_buffer **cmd_buffers =
      (struct tu_cmd_buffer **) vk_submit->command_buffers;
   uint32_t cmdbuf_count = vk_submit->command_buffer_count;

   VkResult result =
      tu_insert_dynamic_cmdbufs(device, &cmd_buffers, &cmdbuf_count);
   if (result != VK_SUCCESS)
      return result;

   bool has_trace_points = false;
   static_assert(offsetof(struct tu_cmd_buffer, vk) == 0,
                 "vk must be first member of tu_cmd_buffer");
   for (unsigned i = 0; i < vk_submit->command_buffer_count; i++) {
      if (u_trace_enabled && u_trace_has_points(&cmd_buffers[i]->trace))
         has_trace_points = true;
   }

   struct tu_u_trace_submission_data *u_trace_submission_data = NULL;

   void *submit = tu_submit_create(device);
   if (!submit)
      goto fail_create_submit;

   if (has_trace_points) {
      tu_u_trace_submission_data_create(
         device, cmd_buffers, cmdbuf_count, &u_trace_submission_data);
   }

   for (uint32_t i = 0; i < cmdbuf_count; i++) {
      struct tu_cmd_buffer *cmd_buffer = cmd_buffers[i];
      struct tu_cs *cs = &cmd_buffer->cs;

      if (perf_pass_index != ~0) {
         struct tu_cs_entry *perf_cs_entry =
            &cmd_buffer->device->perfcntrs_pass_cs_entries[perf_pass_index];

         submit_add_entries(device, submit, &dump_cmds, perf_cs_entry, 1);
      }

      submit_add_entries(device, submit, &dump_cmds, cs->entries,
                         cs->entry_count);

      if (u_trace_submission_data &&
          u_trace_submission_data->cmd_trace_data[i].timestamp_copy_cs) {
         struct tu_cs_entry *trace_cs_entry =
            &u_trace_submission_data->cmd_trace_data[i]
                .timestamp_copy_cs->entries[0];
         submit_add_entries(device, submit, &dump_cmds, trace_cs_entry, 1);
      }
   }

   if (tu_autotune_submit_requires_fence(cmd_buffers, cmdbuf_count)) {
      struct tu_cs *autotune_cs = tu_autotune_on_submit(
         device, &device->autotune, cmd_buffers, cmdbuf_count);
      submit_add_entries(device, submit, &dump_cmds, autotune_cs->entries,
                         autotune_cs->entry_count);
   }

   if (cmdbuf_count && FD_RD_DUMP(ENABLE) &&
       fd_rd_output_begin(&queue->device->rd_output,
                          queue->device->submit_count)) {
      struct tu_device *device = queue->device;
      struct fd_rd_output *rd_output = &device->rd_output;

      if (FD_RD_DUMP(FULL)) {
         VkResult result = tu_queue_wait_fence(queue, queue->fence, ~0);
         if (result != VK_SUCCESS) {
            mesa_loge("FD_RD_DUMP_FULL: wait on previous submission for device %u and queue %d failed: %u",
                      device->device_idx, queue->msm_queue_id, 0);
         }
      }

      fd_rd_output_write_section(rd_output, RD_CHIP_ID, &device->physical_device->dev_id.chip_id, 8);
      fd_rd_output_write_section(rd_output, RD_CMD, "tu-dump", 8);

      mtx_lock(&device->bo_mutex);
      util_dynarray_foreach (&device->dump_bo_list, struct tu_bo *, bo_ptr) {
         struct tu_bo *bo = *bo_ptr;
         uint64_t iova = bo->iova;

         uint32_t buf[3] = { iova, bo->size, iova >> 32 };
         fd_rd_output_write_section(rd_output, RD_GPUADDR, buf, 12);
         if (bo->dump || FD_RD_DUMP(FULL)) {
            tu_bo_map(device, bo, NULL); /* note: this would need locking to be safe */
            fd_rd_output_write_section(rd_output, RD_BUFFER_CONTENTS, bo->map, bo->size);
         }
      }
      mtx_unlock(&device->bo_mutex);

      util_dynarray_foreach (&dump_cmds, struct tu_cs_entry, cmd) {
         uint64_t iova = cmd->bo->iova + cmd->offset;
         uint32_t size = cmd->size >> 2;
         uint32_t buf[3] = { iova, size, iova >> 32 };
         fd_rd_output_write_section(rd_output, RD_CMDSTREAM_ADDR, buf, 12);
      }

      fd_rd_output_end(rd_output);
   }

   util_dynarray_fini(&dump_cmds);

   result =
      tu_queue_submit(queue, submit, vk_submit->waits, vk_submit->wait_count,
                      vk_submit->signals, vk_submit->signal_count,
                      u_trace_submission_data);

   if (result != VK_SUCCESS) {
      pthread_mutex_unlock(&device->submit_mutex);
      goto out;
   }

   tu_debug_bos_print_stats(device);

   if (u_trace_submission_data) {
      u_trace_submission_data->submission_id = device->submit_count;
      u_trace_submission_data->queue = queue;
      u_trace_submission_data->fence = queue->fence;

      for (uint32_t i = 0; i < u_trace_submission_data->cmd_buffer_count; i++) {
         bool free_data = i == u_trace_submission_data->last_buffer_with_tracepoints;
         if (u_trace_submission_data->cmd_trace_data[i].trace)
            u_trace_flush(u_trace_submission_data->cmd_trace_data[i].trace,
                          u_trace_submission_data, queue->device->vk.current_frame,
                          free_data);

         if (!u_trace_submission_data->cmd_trace_data[i].timestamp_copy_cs) {
            /* u_trace is owned by cmd_buffer */
            u_trace_submission_data->cmd_trace_data[i].trace = NULL;
         }
      }
   }

   device->submit_count++;

   pthread_mutex_unlock(&device->submit_mutex);
   pthread_cond_broadcast(&queue->device->timeline_cond);

   u_trace_context_process(&device->trace_context, false);

out:
   tu_submit_finish(device, submit);

fail_create_submit:
   if (cmd_buffers != (struct tu_cmd_buffer **) vk_submit->command_buffers)
      vk_free(&queue->device->vk.alloc, cmd_buffers);

   return result;
}

VkResult
tu_queue_init(struct tu_device *device,
              struct tu_queue *queue,
              int idx,
              const VkDeviceQueueCreateInfo *create_info)
{
   const VkDeviceQueueGlobalPriorityCreateInfoKHR *priority_info =
      vk_find_struct_const(create_info->pNext,
            DEVICE_QUEUE_GLOBAL_PRIORITY_CREATE_INFO_KHR);
   const VkQueueGlobalPriorityKHR global_priority = priority_info ?
      priority_info->globalPriority :
      (TU_DEBUG(HIPRIO) ? VK_QUEUE_GLOBAL_PRIORITY_HIGH_KHR :
       VK_QUEUE_GLOBAL_PRIORITY_MEDIUM_KHR);

   const int priority = tu_get_submitqueue_priority(
         device->physical_device, global_priority,
         device->vk.enabled_features.globalPriorityQuery);
   if (priority < 0) {
      return vk_startup_errorf(device->instance, VK_ERROR_INITIALIZATION_FAILED,
                               "invalid global priority");
   }

   VkResult result = vk_queue_init(&queue->vk, &device->vk, create_info, idx);
   if (result != VK_SUCCESS)
      return result;

   queue->device = device;
   queue->priority = priority;
   queue->vk.driver_submit = queue_submit;

   int ret = tu_drm_submitqueue_new(device, priority, &queue->msm_queue_id);
   if (ret)
      return vk_startup_errorf(device->instance, VK_ERROR_INITIALIZATION_FAILED,
                               "submitqueue create failed");

   queue->fence = -1;

   return VK_SUCCESS;
}

void
tu_queue_finish(struct tu_queue *queue)
{
   vk_queue_finish(&queue->vk);
   tu_drm_submitqueue_close(queue->device, queue->msm_queue_id);
}

