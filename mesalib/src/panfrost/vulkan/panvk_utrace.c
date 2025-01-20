/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "panvk_utrace.h"

#include "kmod/pan_kmod.h"
#include "util/log.h"
#include "util/timespec.h"
#include "panvk_device.h"
#include "panvk_physical_device.h"
#include "panvk_priv_bo.h"
#include "vk_sync.h"

static struct panvk_device *
to_dev(struct u_trace_context *utctx)
{
   return container_of(utctx, struct panvk_device, utrace.utctx);
}

void *
panvk_utrace_create_buffer(struct u_trace_context *utctx, uint64_t size_B)
{
   struct panvk_device *dev = to_dev(utctx);
   struct panvk_priv_bo *bo;

   if (panvk_priv_bo_create(dev, size_B, 0, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE,
                            &bo) != VK_SUCCESS)
      return NULL;

   return bo;
}

void
panvk_utrace_delete_buffer(struct u_trace_context *utctx, void *buffer)
{
   struct panvk_priv_bo *bo = buffer;
   panvk_priv_bo_unref(bo);
}

uint64_t
panvk_utrace_read_ts(struct u_trace_context *utctx, void *timestamps,
                     uint64_t offset_B, void *flush_data)
{
   struct panvk_device *dev = to_dev(utctx);
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   const struct pan_kmod_dev_props *props = &pdev->kmod.props;
   const struct panvk_priv_bo *bo = timestamps;
   struct panvk_utrace_flush_data *data = flush_data;

   assert(props->timestamp_frequency);

   /* wait for the submit */
   if (data->sync) {
      if (vk_sync_wait(&dev->vk, data->sync, data->wait_value,
                       VK_SYNC_WAIT_COMPLETE, UINT64_MAX) != VK_SUCCESS)
         mesa_logw("failed to wait for utrace timestamps");

      data->sync = NULL;
      data->wait_value = 0;
   }

   const uint64_t *ts_ptr = bo->addr.host + offset_B;
   uint64_t ts = *ts_ptr;
   if (ts != U_TRACE_NO_TIMESTAMP)
      ts = (ts * NSEC_PER_SEC) / props->timestamp_frequency;

   return ts;
}

void
panvk_utrace_delete_flush_data(struct u_trace_context *utctx, void *flush_data)
{
   struct panvk_utrace_flush_data *data = flush_data;

   if (data->clone_pool.dev)
      panvk_pool_cleanup(&data->clone_pool);

   free(data);
}
