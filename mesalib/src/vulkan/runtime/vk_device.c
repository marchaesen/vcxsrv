/*
 * Copyright © 2020 Intel Corporation
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

#include "vk_device.h"

#include "vk_common_entrypoints.h"
#include "vk_instance.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_queue.h"
#include "vk_sync.h"
#include "vk_sync_timeline.h"
#include "vk_util.h"
#include "util/u_debug.h"
#include "util/hash_table.h"
#include "util/perf/cpu_trace.h"
#include "util/ralloc.h"
#include "util/timespec.h"

static enum vk_device_timeline_mode
get_timeline_mode(struct vk_physical_device *physical_device)
{
   if (physical_device->supported_sync_types == NULL)
      return VK_DEVICE_TIMELINE_MODE_NONE;

   const struct vk_sync_type *timeline_type = NULL;
   for (const struct vk_sync_type *const *t =
        physical_device->supported_sync_types; *t; t++) {
      if ((*t)->features & VK_SYNC_FEATURE_TIMELINE) {
         /* We can only have one timeline mode */
         assert(timeline_type == NULL);
         timeline_type = *t;
      }
   }

   if (timeline_type == NULL)
      return VK_DEVICE_TIMELINE_MODE_NONE;

   if (vk_sync_type_is_vk_sync_timeline(timeline_type))
      return VK_DEVICE_TIMELINE_MODE_EMULATED;

   if (timeline_type->features & VK_SYNC_FEATURE_WAIT_BEFORE_SIGNAL)
      return VK_DEVICE_TIMELINE_MODE_NATIVE;

   /* For assisted mode, we require a few additional things of all sync types
    * which may be used as semaphores.
    */
   for (const struct vk_sync_type *const *t =
        physical_device->supported_sync_types; *t; t++) {
      if ((*t)->features & VK_SYNC_FEATURE_GPU_WAIT) {
         assert((*t)->features & VK_SYNC_FEATURE_WAIT_PENDING);
         if ((*t)->features & VK_SYNC_FEATURE_BINARY)
            assert((*t)->features & VK_SYNC_FEATURE_CPU_RESET);
      }
   }

   return VK_DEVICE_TIMELINE_MODE_ASSISTED;
}

static void
collect_enabled_features(struct vk_device *device,
                         const VkDeviceCreateInfo *pCreateInfo)
{
   if (pCreateInfo->pEnabledFeatures)
      vk_set_physical_device_features_1_0(&device->enabled_features, pCreateInfo->pEnabledFeatures);
   vk_set_physical_device_features(&device->enabled_features, pCreateInfo->pNext);
}

VkResult
vk_device_init(struct vk_device *device,
               struct vk_physical_device *physical_device,
               const struct vk_device_dispatch_table *dispatch_table,
               const VkDeviceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *alloc)
{
   memset(device, 0, sizeof(*device));
   vk_object_base_init(device, &device->base, VK_OBJECT_TYPE_DEVICE);
   if (alloc != NULL)
      device->alloc = *alloc;
   else
      device->alloc = physical_device->instance->alloc;

   device->physical = physical_device;

   if (dispatch_table) {
      device->dispatch_table = *dispatch_table;

      /* Add common entrypoints without overwriting driver-provided ones. */
      vk_device_dispatch_table_from_entrypoints(
         &device->dispatch_table, &vk_common_device_entrypoints, false);
   }

   for (uint32_t i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
      int idx;
      for (idx = 0; idx < VK_DEVICE_EXTENSION_COUNT; idx++) {
         if (strcmp(pCreateInfo->ppEnabledExtensionNames[i],
                    vk_device_extensions[idx].extensionName) == 0)
            break;
      }

      if (idx >= VK_DEVICE_EXTENSION_COUNT)
         return vk_errorf(physical_device, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);

      if (!physical_device->supported_extensions.extensions[idx])
         return vk_errorf(physical_device, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);

#ifdef ANDROID_STRICT
      if (!vk_android_allowed_device_extensions.extensions[idx])
         return vk_errorf(physical_device, VK_ERROR_EXTENSION_NOT_PRESENT,
                          "%s not supported",
                          pCreateInfo->ppEnabledExtensionNames[i]);
#endif

      device->enabled_extensions.extensions[idx] = true;
   }

   VkResult result =
      vk_physical_device_check_device_features(physical_device,
                                               pCreateInfo);
   if (result != VK_SUCCESS)
      return result;

   collect_enabled_features(device, pCreateInfo);

   p_atomic_set(&device->private_data_next_index, 0);

   list_inithead(&device->queues);

   device->drm_fd = -1;
   device->mem_cache = NULL;

   device->timeline_mode = get_timeline_mode(physical_device);

   switch (device->timeline_mode) {
   case VK_DEVICE_TIMELINE_MODE_NONE:
   case VK_DEVICE_TIMELINE_MODE_NATIVE:
      device->submit_mode = VK_QUEUE_SUBMIT_MODE_IMMEDIATE;
      break;

   case VK_DEVICE_TIMELINE_MODE_EMULATED:
      device->submit_mode = VK_QUEUE_SUBMIT_MODE_DEFERRED;
      break;

   case VK_DEVICE_TIMELINE_MODE_ASSISTED:
      if (os_get_option("MESA_VK_ENABLE_SUBMIT_THREAD")) {
         if (debug_get_bool_option("MESA_VK_ENABLE_SUBMIT_THREAD", false)) {
            device->submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED;
         } else {
            device->submit_mode = VK_QUEUE_SUBMIT_MODE_IMMEDIATE;
         }
      } else {
         device->submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED_ON_DEMAND;
      }
      break;

   default:
      unreachable("Invalid timeline mode");
   }

#if DETECT_OS_ANDROID
   mtx_init(&device->swapchain_private_mtx, mtx_plain);
   device->swapchain_private = NULL;
#endif /* DETECT_OS_ANDROID */

   simple_mtx_init(&device->trace_mtx, mtx_plain);

   vk_foreach_struct_const (ext, pCreateInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_DEVICE_PIPELINE_BINARY_INTERNAL_CACHE_CONTROL_KHR: {
         const VkDevicePipelineBinaryInternalCacheControlKHR *cache_control = (const void *)ext;
         if (cache_control->disableInternalCache)
            device->disable_internal_cache = true;
         break;
      }
      default:
         break;
      }
   }

   if (device->enabled_extensions.KHR_calibrated_timestamps ||
       device->enabled_extensions.EXT_calibrated_timestamps) {
      /* sorted by preference */
      const VkTimeDomainKHR calibrate_domains[] = {
         VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR,
         VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
      };
      for (uint32_t i = 0; i < ARRAY_SIZE(calibrate_domains); i++) {
         const VkTimeDomainKHR domain = calibrate_domains[i];
         uint64_t ts;
         if (vk_device_get_timestamp(NULL, domain, &ts) == VK_SUCCESS) {
            device->calibrate_time_domain = domain;
            break;
         }
      }

      assert(device->calibrate_time_domain != VK_TIME_DOMAIN_DEVICE_KHR);
      device->device_time_domain_period =
         (uint64_t)ceilf(device->physical->properties.timestampPeriod);
   }

   return VK_SUCCESS;
}

void
vk_device_finish(struct vk_device *device)
{
   /* Drivers should tear down their own queues */
   assert(list_is_empty(&device->queues));

   vk_memory_trace_finish(device);

#if DETECT_OS_ANDROID
   if (device->swapchain_private) {
      hash_table_foreach(device->swapchain_private, entry)
         util_sparse_array_finish(entry->data);
      ralloc_free(device->swapchain_private);
   }
#endif /* DETECT_OS_ANDROID */

   simple_mtx_destroy(&device->trace_mtx);

   vk_object_base_finish(&device->base);
}

void
vk_device_enable_threaded_submit(struct vk_device *device)
{
   /* This must be called before any queues are created */
   assert(list_is_empty(&device->queues));

   /* In order to use threaded submit, we need every sync type that can be
    * used as a wait fence for vkQueueSubmit() to support WAIT_PENDING.
    * It's required for cross-thread/process submit re-ordering.
    */
   for (const struct vk_sync_type *const *t =
        device->physical->supported_sync_types; *t; t++) {
      if ((*t)->features & VK_SYNC_FEATURE_GPU_WAIT)
         assert((*t)->features & VK_SYNC_FEATURE_WAIT_PENDING);
   }

   /* Any binary vk_sync types which will be used as permanent semaphore
    * payloads also need to support vk_sync_type::move, but that's a lot
    * harder to assert since it only applies to permanent semaphore payloads.
    */

   if (device->submit_mode != VK_QUEUE_SUBMIT_MODE_THREADED)
      device->submit_mode = VK_QUEUE_SUBMIT_MODE_THREADED_ON_DEMAND;
}

VkResult
vk_device_flush(struct vk_device *device)
{
   if (device->submit_mode != VK_QUEUE_SUBMIT_MODE_DEFERRED)
      return VK_SUCCESS;

   bool progress;
   do {
      progress = false;

      vk_foreach_queue(queue, device) {
         uint32_t queue_submit_count;
         VkResult result = vk_queue_flush(queue, &queue_submit_count);
         if (unlikely(result != VK_SUCCESS))
            return result;

         if (queue_submit_count)
            progress = true;
      }
   } while (progress);

   return VK_SUCCESS;
}

static const char *
timeline_mode_str(struct vk_device *device)
{
   switch (device->timeline_mode) {
#define CASE(X) case VK_DEVICE_TIMELINE_MODE_##X: return #X;
   CASE(NONE)
   CASE(EMULATED)
   CASE(ASSISTED)
   CASE(NATIVE)
#undef CASE
   default: return "UNKNOWN";
   }
}

void
_vk_device_report_lost(struct vk_device *device)
{
   assert(p_atomic_read(&device->_lost.lost) > 0);

   device->_lost.reported = true;

   vk_foreach_queue(queue, device) {
      if (queue->_lost.lost) {
         __vk_errorf(queue, VK_ERROR_DEVICE_LOST,
                     queue->_lost.error_file, queue->_lost.error_line,
                     "%s", queue->_lost.error_msg);
      }
   }

   vk_logd(VK_LOG_OBJS(device), "Timeline mode is %s.",
           timeline_mode_str(device));
}

VkResult
_vk_device_set_lost(struct vk_device *device,
                    const char *file, int line,
                    const char *msg, ...)
{
   /* This flushes out any per-queue device lost messages */
   if (vk_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   p_atomic_inc(&device->_lost.lost);
   device->_lost.reported = true;

   va_list ap;
   va_start(ap, msg);
   __vk_errorv(device, VK_ERROR_DEVICE_LOST, file, line, msg, ap);
   va_end(ap);

   vk_logd(VK_LOG_OBJS(device), "Timeline mode is %s.",
           timeline_mode_str(device));

   if (debug_get_bool_option("MESA_VK_ABORT_ON_DEVICE_LOSS", false))
      abort();

   return VK_ERROR_DEVICE_LOST;
}

PFN_vkVoidFunction
vk_device_get_proc_addr(const struct vk_device *device,
                        const char *name)
{
   if (device == NULL || name == NULL)
      return NULL;

   struct vk_instance *instance = device->physical->instance;
   return vk_device_dispatch_table_get_if_supported(&device->dispatch_table,
                                                    name,
                                                    instance->app_info.api_version,
                                                    &instance->enabled_extensions,
                                                    &device->enabled_extensions);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL
vk_common_GetDeviceProcAddr(VkDevice _device,
                            const char *pName)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   return vk_device_get_proc_addr(device, pName);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetDeviceQueue(VkDevice _device,
                         uint32_t queueFamilyIndex,
                         uint32_t queueIndex,
                         VkQueue *pQueue)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   const VkDeviceQueueInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_INFO_2,
      .pNext = NULL,
      /* flags = 0 because (Vulkan spec 1.2.170 - vkGetDeviceQueue):
       *
       *    "vkGetDeviceQueue must only be used to get queues that were
       *     created with the flags parameter of VkDeviceQueueCreateInfo set
       *     to zero. To get queues that were created with a non-zero flags
       *     parameter use vkGetDeviceQueue2."
       */
      .flags = 0,
      .queueFamilyIndex = queueFamilyIndex,
      .queueIndex = queueIndex,
   };

   device->dispatch_table.GetDeviceQueue2(_device, &info, pQueue);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetDeviceQueue2(VkDevice _device,
                          const VkDeviceQueueInfo2 *pQueueInfo,
                          VkQueue *pQueue)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   struct vk_queue *queue = NULL;
   vk_foreach_queue(iter, device) {
      if (iter->queue_family_index == pQueueInfo->queueFamilyIndex &&
          iter->index_in_family == pQueueInfo->queueIndex) {
         queue = iter;
         break;
      }
   }

   /* From the Vulkan 1.1.70 spec:
    *
    *    "The queue returned by vkGetDeviceQueue2 must have the same flags
    *    value from this structure as that used at device creation time in a
    *    VkDeviceQueueCreateInfo instance. If no matching flags were specified
    *    at device creation time then pQueue will return VK_NULL_HANDLE."
    */
   if (queue && queue->flags == pQueueInfo->flags)
      *pQueue = vk_queue_to_handle(queue);
   else
      *pQueue = VK_NULL_HANDLE;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_MapMemory(VkDevice _device,
                    VkDeviceMemory memory,
                    VkDeviceSize offset,
                    VkDeviceSize size,
                    VkMemoryMapFlags flags,
                    void **ppData)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   const VkMemoryMapInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_MAP_INFO_KHR,
      .flags = flags,
      .memory = memory,
      .offset = offset,
      .size = size,
   };

   return device->dispatch_table.MapMemory2KHR(_device, &info, ppData);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_UnmapMemory(VkDevice _device,
                      VkDeviceMemory memory)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   ASSERTED VkResult result;

   const VkMemoryUnmapInfoKHR info = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_UNMAP_INFO_KHR,
      .memory = memory,
   };

   result = device->dispatch_table.UnmapMemory2KHR(_device, &info);
   assert(result == VK_SUCCESS);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetDeviceGroupPeerMemoryFeatures(
   VkDevice device,
   uint32_t heapIndex,
   uint32_t localDeviceIndex,
   uint32_t remoteDeviceIndex,
   VkPeerMemoryFeatureFlags *pPeerMemoryFeatures)
{
   assert(localDeviceIndex == 0 && remoteDeviceIndex == 0);
   *pPeerMemoryFeatures = VK_PEER_MEMORY_FEATURE_COPY_SRC_BIT |
                          VK_PEER_MEMORY_FEATURE_COPY_DST_BIT |
                          VK_PEER_MEMORY_FEATURE_GENERIC_SRC_BIT |
                          VK_PEER_MEMORY_FEATURE_GENERIC_DST_BIT;
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetImageMemoryRequirements(VkDevice _device,
                                     VkImage image,
                                     VkMemoryRequirements *pMemoryRequirements)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkImageMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };
   VkMemoryRequirements2 reqs = {
      .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
   };
   device->dispatch_table.GetImageMemoryRequirements2(_device, &info, &reqs);

   *pMemoryRequirements = reqs.memoryRequirements;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_BindImageMemory(VkDevice _device,
                          VkImage image,
                          VkDeviceMemory memory,
                          VkDeviceSize memoryOffset)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkBindImageMemoryInfo bind = {
      .sType         = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
      .image         = image,
      .memory        = memory,
      .memoryOffset  = memoryOffset,
   };

   return device->dispatch_table.BindImageMemory2(_device, 1, &bind);
}

VKAPI_ATTR void VKAPI_CALL
vk_common_GetImageSparseMemoryRequirements(VkDevice _device,
                                           VkImage image,
                                           uint32_t *pSparseMemoryRequirementCount,
                                           VkSparseImageMemoryRequirements *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(vk_device, device, _device);

   VkImageSparseMemoryRequirementsInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_SPARSE_MEMORY_REQUIREMENTS_INFO_2,
      .image = image,
   };

   if (!pSparseMemoryRequirements) {
      device->dispatch_table.GetImageSparseMemoryRequirements2(_device,
                                                               &info,
                                                               pSparseMemoryRequirementCount,
                                                               NULL);
      return;
   }

   STACK_ARRAY(VkSparseImageMemoryRequirements2, mem_reqs2, *pSparseMemoryRequirementCount);

   for (unsigned i = 0; i < *pSparseMemoryRequirementCount; ++i) {
      mem_reqs2[i].sType = VK_STRUCTURE_TYPE_SPARSE_IMAGE_MEMORY_REQUIREMENTS_2;
      mem_reqs2[i].pNext = NULL;
   }

   device->dispatch_table.GetImageSparseMemoryRequirements2(_device,
                                                            &info,
                                                            pSparseMemoryRequirementCount,
                                                            mem_reqs2);

   for (unsigned i = 0; i < *pSparseMemoryRequirementCount; ++i)
      pSparseMemoryRequirements[i] = mem_reqs2[i].memoryRequirements;

   STACK_ARRAY_FINISH(mem_reqs2);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_DeviceWaitIdle(VkDevice _device)
{
   MESA_TRACE_FUNC();

   VK_FROM_HANDLE(vk_device, device, _device);
   const struct vk_device_dispatch_table *disp = &device->dispatch_table;

   vk_foreach_queue(queue, device) {
      VkResult result = disp->QueueWaitIdle(vk_queue_to_handle(queue));
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

VkResult
vk_device_get_timestamp(struct vk_device *device, VkTimeDomainKHR domain,
                        uint64_t *timestamp)
{
   if (domain == VK_TIME_DOMAIN_DEVICE_KHR) {
      assert(device && device->get_timestamp);
      return device->get_timestamp(device, timestamp);
   }

   /* device is not used for host time domains */
#ifndef _WIN32
   clockid_t clockid;
   struct timespec ts;

   switch (domain) {
   case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
      clockid = CLOCK_MONOTONIC;
      break;
   case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
      /* The "RAW" clocks on Linux are called "FAST" on FreeBSD */
#if defined(CLOCK_MONOTONIC_RAW)
      clockid = CLOCK_MONOTONIC_RAW;
      break;
#elif defined(CLOCK_MONOTONIC_FAST)
      clockid = CLOCK_MONOTONIC_FAST;
      break;
#else
      FALLTHROUGH;
#endif
   default:
      goto fail;
   }

   if (clock_gettime(clockid, &ts) < 0)
      goto fail;

   *timestamp = (uint64_t)ts.tv_sec * NSEC_PER_SEC + ts.tv_nsec;
   return VK_SUCCESS;

fail:
#endif /* _WIN32 */
   return VK_ERROR_FEATURE_NOT_PRESENT;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetCalibratedTimestampsKHR(
   VkDevice _device, uint32_t timestampCount,
   const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
   uint64_t *pMaxDeviation)
{
   VK_FROM_HANDLE(vk_device, device, _device);
   uint64_t begin, end;
   VkResult result;

   /* collect timestamps as tight as possible */
   result =
      vk_device_get_timestamp(device, device->calibrate_time_domain, &begin);
   for (uint32_t i = 0; i < timestampCount; i++) {
      const VkTimeDomainKHR domain = pTimestampInfos[i].timeDomain;
      if (domain == device->calibrate_time_domain)
         pTimestamps[i] = begin;
      else
         result |= vk_device_get_timestamp(device, domain, &pTimestamps[i]);
   }
   result |=
      vk_device_get_timestamp(device, device->calibrate_time_domain, &end);

   if (result != VK_SUCCESS)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   uint64_t max_clock_period = 0;
   for (uint32_t i = 0; i < timestampCount; i++) {
      const VkTimeDomainKHR domain = pTimestampInfos[i].timeDomain;
      const uint64_t period = domain == VK_TIME_DOMAIN_DEVICE_KHR
         ? device->device_time_domain_period
         : domain != device->calibrate_time_domain ? 1 : 0;
      max_clock_period = MAX2(max_clock_period, period);
   }

   *pMaxDeviation = vk_time_max_deviation(begin, end, max_clock_period);

   return VK_SUCCESS;
}

#ifndef _WIN32

uint64_t
vk_clock_gettime(clockid_t clock_id)
{
   struct timespec current;
   int ret;

   ret = clock_gettime(clock_id, &current);
#ifdef CLOCK_MONOTONIC_RAW
   if (ret < 0 && clock_id == CLOCK_MONOTONIC_RAW)
      ret = clock_gettime(CLOCK_MONOTONIC, &current);
#endif
   if (ret < 0)
      return 0;

   return (uint64_t)current.tv_sec * 1000000000ULL + current.tv_nsec;
}

#endif //!_WIN32
