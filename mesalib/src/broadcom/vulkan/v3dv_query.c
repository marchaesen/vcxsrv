/*
 * Copyright © 2020 Raspberry Pi Ltd
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

#include "v3dv_private.h"

#include "util/timespec.h"

static const char *v3dv_counters[][3] = {
   {"FEP", "FEP-valid-primitives-no-rendered-pixels", "[FEP] Valid primitives that result in no rendered pixels, for all rendered tiles"},
   {"FEP", "FEP-valid-primitives-rendered-pixels", "[FEP] Valid primitives for all rendered tiles (primitives may be counted in more than one tile)"},
   {"FEP", "FEP-clipped-quads", "[FEP] Early-Z/Near/Far clipped quads"},
   {"FEP", "FEP-valid-quads", "[FEP] Valid quads"},
   {"TLB", "TLB-quads-not-passing-stencil-test", "[TLB] Quads with no pixels passing the stencil test"},
   {"TLB", "TLB-quads-not-passing-z-and-stencil-test", "[TLB] Quads with no pixels passing the Z and stencil tests"},
   {"TLB", "TLB-quads-passing-z-and-stencil-test", "[TLB] Quads with any pixels passing the Z and stencil tests"},
   {"TLB", "TLB-quads-with-zero-coverage", "[TLB] Quads with all pixels having zero coverage"},
   {"TLB", "TLB-quads-with-non-zero-coverage", "[TLB] Quads with any pixels having non-zero coverage"},
   {"TLB", "TLB-quads-written-to-color-buffer", "[TLB] Quads with valid pixels written to colour buffer"},
   {"PTB", "PTB-primitives-discarded-outside-viewport", "[PTB] Primitives discarded by being outside the viewport"},
   {"PTB", "PTB-primitives-need-clipping", "[PTB] Primitives that need clipping"},
   {"PTB", "PTB-primitives-discared-reversed", "[PTB] Primitives that are discarded because they are reversed"},
   {"QPU", "QPU-total-idle-clk-cycles", "[QPU] Total idle clock cycles for all QPUs"},
   {"QPU", "QPU-total-active-clk-cycles-vertex-coord-shading", "[QPU] Total active clock cycles for all QPUs doing vertex/coordinate/user shading (counts only when QPU is not stalled)"},
   {"QPU", "QPU-total-active-clk-cycles-fragment-shading", "[QPU] Total active clock cycles for all QPUs doing fragment shading (counts only when QPU is not stalled)"},
   {"QPU", "QPU-total-clk-cycles-executing-valid-instr", "[QPU] Total clock cycles for all QPUs executing valid instructions"},
   {"QPU", "QPU-total-clk-cycles-waiting-TMU", "[QPU] Total clock cycles for all QPUs stalled waiting for TMUs only (counter won't increment if QPU also stalling for another reason)"},
   {"QPU", "QPU-total-clk-cycles-waiting-scoreboard", "[QPU] Total clock cycles for all QPUs stalled waiting for Scoreboard only (counter won't increment if QPU also stalling for another reason)"},
   {"QPU", "QPU-total-clk-cycles-waiting-varyings", "[QPU] Total clock cycles for all QPUs stalled waiting for Varyings only (counter won't increment if QPU also stalling for another reason)"},
   {"QPU", "QPU-total-instr-cache-hit", "[QPU] Total instruction cache hits for all slices"},
   {"QPU", "QPU-total-instr-cache-miss", "[QPU] Total instruction cache misses for all slices"},
   {"QPU", "QPU-total-uniform-cache-hit", "[QPU] Total uniforms cache hits for all slices"},
   {"QPU", "QPU-total-uniform-cache-miss", "[QPU] Total uniforms cache misses for all slices"},
   {"TMU", "TMU-total-text-quads-access", "[TMU] Total texture cache accesses"},
   {"TMU", "TMU-total-text-cache-miss", "[TMU] Total texture cache misses (number of fetches from memory/L2cache)"},
   {"VPM", "VPM-total-clk-cycles-VDW-stalled", "[VPM] Total clock cycles VDW is stalled waiting for VPM access"},
   {"VPM", "VPM-total-clk-cycles-VCD-stalled", "[VPM] Total clock cycles VCD is stalled waiting for VPM access"},
   {"CLE", "CLE-bin-thread-active-cycles", "[CLE] Bin thread active cycles"},
   {"CLE", "CLE-render-thread-active-cycles", "[CLE] Render thread active cycles"},
   {"L2T", "L2T-total-cache-hit", "[L2T] Total Level 2 cache hits"},
   {"L2T", "L2T-total-cache-miss", "[L2T] Total Level 2 cache misses"},
   {"CORE", "cycle-count", "[CORE] Cycle counter"},
   {"QPU", "QPU-total-clk-cycles-waiting-vertex-coord-shading", "[QPU] Total stalled clock cycles for all QPUs doing vertex/coordinate/user shading"},
   {"QPU", "QPU-total-clk-cycles-waiting-fragment-shading", "[QPU] Total stalled clock cycles for all QPUs doing fragment shading"},
   {"PTB", "PTB-primitives-binned", "[PTB] Total primitives binned"},
   {"AXI", "AXI-writes-seen-watch-0", "[AXI] Writes seen by watch 0"},
   {"AXI", "AXI-reads-seen-watch-0", "[AXI] Reads seen by watch 0"},
   {"AXI", "AXI-writes-stalled-seen-watch-0", "[AXI] Write stalls seen by watch 0"},
   {"AXI", "AXI-reads-stalled-seen-watch-0", "[AXI] Read stalls seen by watch 0"},
   {"AXI", "AXI-write-bytes-seen-watch-0", "[AXI] Total bytes written seen by watch 0"},
   {"AXI", "AXI-read-bytes-seen-watch-0", "[AXI] Total bytes read seen by watch 0"},
   {"AXI", "AXI-writes-seen-watch-1", "[AXI] Writes seen by watch 1"},
   {"AXI", "AXI-reads-seen-watch-1", "[AXI] Reads seen by watch 1"},
   {"AXI", "AXI-writes-stalled-seen-watch-1", "[AXI] Write stalls seen by watch 1"},
   {"AXI", "AXI-reads-stalled-seen-watch-1", "[AXI] Read stalls seen by watch 1"},
   {"AXI", "AXI-write-bytes-seen-watch-1", "[AXI] Total bytes written seen by watch 1"},
   {"AXI", "AXI-read-bytes-seen-watch-1", "[AXI] Total bytes read seen by watch 1"},
   {"TLB", "TLB-partial-quads-written-to-color-buffer", "[TLB] Partial quads written to the colour buffer"},
   {"TMU", "TMU-total-config-access", "[TMU] Total config accesses"},
   {"L2T", "L2T-no-id-stalled", "[L2T] No ID stall"},
   {"L2T", "L2T-command-queue-stalled", "[L2T] Command queue full stall"},
   {"L2T", "L2T-TMU-writes", "[L2T] TMU write accesses"},
   {"TMU", "TMU-active-cycles", "[TMU] Active cycles"},
   {"TMU", "TMU-stalled-cycles", "[TMU] Stalled cycles"},
   {"CLE", "CLE-thread-active-cycles", "[CLE] Bin or render thread active cycles"},
   {"L2T", "L2T-TMU-reads", "[L2T] TMU read accesses"},
   {"L2T", "L2T-CLE-reads", "[L2T] CLE read accesses"},
   {"L2T", "L2T-VCD-reads", "[L2T] VCD read accesses"},
   {"L2T", "L2T-TMU-config-reads", "[L2T] TMU CFG read accesses"},
   {"L2T", "L2T-SLC0-reads", "[L2T] SLC0 read accesses"},
   {"L2T", "L2T-SLC1-reads", "[L2T] SLC1 read accesses"},
   {"L2T", "L2T-SLC2-reads", "[L2T] SLC2 read accesses"},
   {"L2T", "L2T-TMU-write-miss", "[L2T] TMU write misses"},
   {"L2T", "L2T-TMU-read-miss", "[L2T] TMU read misses"},
   {"L2T", "L2T-CLE-read-miss", "[L2T] CLE read misses"},
   {"L2T", "L2T-VCD-read-miss", "[L2T] VCD read misses"},
   {"L2T", "L2T-TMU-config-read-miss", "[L2T] TMU CFG read misses"},
   {"L2T", "L2T-SLC0-read-miss", "[L2T] SLC0 read misses"},
   {"L2T", "L2T-SLC1-read-miss", "[L2T] SLC1 read misses"},
   {"L2T", "L2T-SLC2-read-miss", "[L2T] SLC2 read misses"},
   {"CORE", "core-memory-writes", "[CORE] Total memory writes"},
   {"L2T", "L2T-memory-writes", "[L2T] Total memory writes"},
   {"PTB", "PTB-memory-writes", "[PTB] Total memory writes"},
   {"TLB", "TLB-memory-writes", "[TLB] Total memory writes"},
   {"CORE", "core-memory-reads", "[CORE] Total memory reads"},
   {"L2T", "L2T-memory-reads", "[L2T] Total memory reads"},
   {"PTB", "PTB-memory-reads", "[PTB] Total memory reads"},
   {"PSE", "PSE-memory-reads", "[PSE] Total memory reads"},
   {"TLB", "TLB-memory-reads", "[TLB] Total memory reads"},
   {"GMP", "GMP-memory-reads", "[GMP] Total memory reads"},
   {"PTB", "PTB-memory-words-writes", "[PTB] Total memory words written"},
   {"TLB", "TLB-memory-words-writes", "[TLB] Total memory words written"},
   {"PSE", "PSE-memory-words-reads", "[PSE] Total memory words read"},
   {"TLB", "TLB-memory-words-reads", "[TLB] Total memory words read"},
   {"TMU", "TMU-MRU-hits", "[TMU] Total MRU hits"},
   {"CORE", "compute-active-cycles", "[CORE] Compute active cycles"},
};

static void
kperfmon_create(struct v3dv_device *device,
                struct v3dv_query_pool *pool,
                uint32_t query)
{
   for (uint32_t i = 0; i < pool->perfmon.nperfmons; i++) {
      assert(i * DRM_V3D_MAX_PERF_COUNTERS < pool->perfmon.ncounters);

      struct drm_v3d_perfmon_create req = {
         .ncounters = MIN2(pool->perfmon.ncounters -
                           i * DRM_V3D_MAX_PERF_COUNTERS,
                           DRM_V3D_MAX_PERF_COUNTERS),
      };
      memcpy(req.counters,
             &pool->perfmon.counters[i * DRM_V3D_MAX_PERF_COUNTERS],
             req.ncounters);

      int ret = v3dv_ioctl(device->pdevice->render_fd,
                           DRM_IOCTL_V3D_PERFMON_CREATE,
                           &req);
      if (ret)
         fprintf(stderr, "Failed to create perfmon: %s\n", strerror(ret));

      pool->queries[query].perf.kperfmon_ids[i] = req.id;
   }
}

static void
kperfmon_destroy(struct v3dv_device *device,
                 struct v3dv_query_pool *pool,
                 uint32_t query)
{
   /* Skip destroying if never created */
   if (!pool->queries[query].perf.kperfmon_ids[0])
      return;

   for (uint32_t i = 0; i < pool->perfmon.nperfmons; i++) {
      struct drm_v3d_perfmon_destroy req = {
         .id = pool->queries[query].perf.kperfmon_ids[i]
      };

      int ret = v3dv_ioctl(device->pdevice->render_fd,
                           DRM_IOCTL_V3D_PERFMON_DESTROY,
                           &req);

      if (ret) {
         fprintf(stderr, "Failed to destroy perfmon %u: %s\n",
                 req.id, strerror(ret));
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateQueryPool(VkDevice _device,
                     const VkQueryPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkQueryPool *pQueryPool)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   assert(pCreateInfo->queryType == VK_QUERY_TYPE_OCCLUSION ||
          pCreateInfo->queryType == VK_QUERY_TYPE_TIMESTAMP ||
          pCreateInfo->queryType == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR);
   assert(pCreateInfo->queryCount > 0);

   struct v3dv_query_pool *pool =
      vk_object_zalloc(&device->vk, pAllocator, sizeof(*pool),
                       VK_OBJECT_TYPE_QUERY_POOL);
   if (pool == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pool->query_type = pCreateInfo->queryType;
   pool->query_count = pCreateInfo->queryCount;

   uint32_t query_idx = 0;
   VkResult result;

   const uint32_t pool_bytes = sizeof(struct v3dv_query) * pool->query_count;
   pool->queries = vk_alloc2(&device->vk.alloc, pAllocator, pool_bytes, 8,
                             VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool->queries == NULL) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }

   switch (pool->query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      /* The hardware allows us to setup groups of 16 queries in consecutive
       * 4-byte addresses, requiring only that each group of 16 queries is
       * aligned to a 1024 byte boundary.
       */
      const uint32_t query_groups = DIV_ROUND_UP(pool->query_count, 16);
      const uint32_t bo_size = query_groups * 1024;
      pool->bo = v3dv_bo_alloc(device, bo_size, "query", true);
      if (!pool->bo) {
         result = vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto fail;
      }
      if (!v3dv_bo_map(device, pool->bo, bo_size)) {
         result = vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         goto fail;
      }
      break;
   }
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
      const VkQueryPoolPerformanceCreateInfoKHR *pq_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              QUERY_POOL_PERFORMANCE_CREATE_INFO_KHR);

      assert(pq_info);
      assert(pq_info->counterIndexCount <= V3D_PERFCNT_NUM);

      pool->perfmon.ncounters = pq_info->counterIndexCount;
      for (uint32_t i = 0; i < pq_info->counterIndexCount; i++)
         pool->perfmon.counters[i] = pq_info->pCounterIndices[i];

      pool->perfmon.nperfmons = DIV_ROUND_UP(pool->perfmon.ncounters,
                                             DRM_V3D_MAX_PERF_COUNTERS);

      assert(pool->perfmon.nperfmons <= V3DV_MAX_PERFMONS);
      break;
   }
   case VK_QUERY_TYPE_TIMESTAMP:
      break;
   default:
      unreachable("Unsupported query type");
   }

   for (; query_idx < pool->query_count; query_idx++) {
      pool->queries[query_idx].maybe_available = false;
      switch (pool->query_type) {
      case VK_QUERY_TYPE_OCCLUSION: {
         const uint32_t query_group = query_idx / 16;
         const uint32_t query_offset = query_group * 1024 + (query_idx % 16) * 4;
         pool->queries[query_idx].bo = pool->bo;
         pool->queries[query_idx].offset = query_offset;
         break;
         }
      case VK_QUERY_TYPE_TIMESTAMP:
         pool->queries[query_idx].value = 0;
         break;
      case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR: {
         result = vk_sync_create(&device->vk,
                                 &device->pdevice->drm_syncobj_type, 0, 0,
                                 &pool->queries[query_idx].perf.last_job_sync);
         if (result != VK_SUCCESS)
            goto fail;

         for (uint32_t j = 0; j < pool->perfmon.nperfmons; j++)
            pool->queries[query_idx].perf.kperfmon_ids[j] = 0;
         break;
         }
      default:
         unreachable("Unsupported query type");
      }
   }

   *pQueryPool = v3dv_query_pool_to_handle(pool);

   return VK_SUCCESS;

fail:
   if (pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      for (uint32_t j = 0; j < query_idx; j++)
         vk_sync_destroy(&device->vk, pool->queries[j].perf.last_job_sync);
   }

   if (pool->bo)
      v3dv_bo_free(device, pool->bo);
   if (pool->queries)
      vk_free2(&device->vk.alloc, pAllocator, pool->queries);
   vk_object_free(&device->vk, pAllocator, pool);

   return result;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyQueryPool(VkDevice _device,
                      VkQueryPool queryPool,
                      const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);

   if (!pool)
      return;

   if (pool->bo)
      v3dv_bo_free(device, pool->bo);

   if (pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      for (uint32_t i = 0; i < pool->query_count; i++) {
         kperfmon_destroy(device, pool, i);
         vk_sync_destroy(&device->vk, pool->queries[i].perf.last_job_sync);
      }
   }

   if (pool->queries)
      vk_free2(&device->vk.alloc, pAllocator, pool->queries);

   vk_object_free(&device->vk, pAllocator, pool);
}

static void
write_to_buffer(void *dst, uint32_t idx, bool do_64bit, uint64_t value)
{
   if (do_64bit) {
      uint64_t *dst64 = (uint64_t *) dst;
      dst64[idx] = value;
   } else {
      uint32_t *dst32 = (uint32_t *) dst;
      dst32[idx] = (uint32_t) value;
   }
}

static VkResult
query_wait_available(struct v3dv_device *device,
                     struct v3dv_query *q,
                     VkQueryType query_type)
{
   if (!q->maybe_available) {
      struct timespec timeout;
      timespec_get(&timeout, TIME_UTC);
      timespec_add_msec(&timeout, &timeout, 2000);

      VkResult result = VK_SUCCESS;

      mtx_lock(&device->query_mutex);
      while (!q->maybe_available) {
         if (vk_device_is_lost(&device->vk)) {
            result = VK_ERROR_DEVICE_LOST;
            break;
         }

         int ret = cnd_timedwait(&device->query_ended,
                                 &device->query_mutex,
                                 &timeout);
         if (ret != thrd_success) {
            mtx_unlock(&device->query_mutex);
            result = vk_device_set_lost(&device->vk, "Query wait failed");
            break;
         }
      }
      mtx_unlock(&device->query_mutex);

      if (result != VK_SUCCESS)
         return result;
   }

   if (query_type == VK_QUERY_TYPE_OCCLUSION &&
       !v3dv_bo_wait(device, q->bo, 0xffffffffffffffffull))
      return vk_device_set_lost(&device->vk, "Query BO wait failed: %m");

   if (query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR &&
       vk_sync_wait(&device->vk, q->perf.last_job_sync,
                    0, VK_SYNC_WAIT_COMPLETE, UINT64_MAX) != VK_SUCCESS)
      return vk_device_set_lost(&device->vk, "Query job wait failed");

   return VK_SUCCESS;
}

static VkResult
write_occlusion_query_result(struct v3dv_device *device,
                             struct v3dv_query_pool *pool,
                             uint32_t query,
                             bool do_64bit,
                             void *data,
                             uint32_t slot)
{
   assert(pool && pool->query_type == VK_QUERY_TYPE_OCCLUSION);

   if (vk_device_is_lost(&device->vk))
      return VK_ERROR_DEVICE_LOST;

   struct v3dv_query *q = &pool->queries[query];
   assert(q->bo && q->bo->map);

   const uint8_t *query_addr = ((uint8_t *) q->bo->map) + q->offset;
   write_to_buffer(data, slot, do_64bit, (uint64_t) *((uint32_t *)query_addr));
   return VK_SUCCESS;
}

static VkResult
write_timestamp_query_result(struct v3dv_device *device,
                             struct v3dv_query_pool *pool,
                             uint32_t query,
                             bool do_64bit,
                             void *data,
                             uint32_t slot)
{
   assert(pool && pool->query_type == VK_QUERY_TYPE_TIMESTAMP);

   struct v3dv_query *q = &pool->queries[query];

   write_to_buffer(data, slot, do_64bit, q->value);
   return VK_SUCCESS;
}

static VkResult
write_performance_query_result(struct v3dv_device *device,
                               struct v3dv_query_pool *pool,
                               uint32_t query,
                               bool do_64bit,
                               void *data,
                               uint32_t slot)
{
   assert(pool && pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR);

   struct v3dv_query *q = &pool->queries[query];
   uint64_t counter_values[V3D_PERFCNT_NUM];

   for (uint32_t i = 0; i < pool->perfmon.nperfmons; i++) {
      struct drm_v3d_perfmon_get_values req = {
         .id = q->perf.kperfmon_ids[i],
         .values_ptr = (uintptr_t)(&counter_values[i *
                                   DRM_V3D_MAX_PERF_COUNTERS])
      };

      int ret = v3dv_ioctl(device->pdevice->render_fd,
                           DRM_IOCTL_V3D_PERFMON_GET_VALUES,
                           &req);

      if (ret) {
         fprintf(stderr, "failed to get perfmon values: %s\n", strerror(ret));
         return vk_error(device, VK_ERROR_DEVICE_LOST);
      }
   }

   for (uint32_t i = 0; i < pool->perfmon.ncounters; i++)
      write_to_buffer(data, slot + i, do_64bit, counter_values[i]);

   return VK_SUCCESS;
}

static VkResult
query_check_available(struct v3dv_device *device,
                      struct v3dv_query *q,
                      VkQueryType query_type)
{
   if (!q->maybe_available)
      return VK_NOT_READY;

   if (query_type == VK_QUERY_TYPE_OCCLUSION &&
       !v3dv_bo_wait(device, q->bo, 0))
      return VK_NOT_READY;

   if (query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR &&
       vk_sync_wait(&device->vk, q->perf.last_job_sync,
                    0, VK_SYNC_WAIT_COMPLETE, 0) != VK_SUCCESS)
      return VK_NOT_READY;

   return VK_SUCCESS;
}

static VkResult
write_query_result(struct v3dv_device *device,
                   struct v3dv_query_pool *pool,
                   uint32_t query,
                   bool do_64bit,
                   void *data,
                   uint32_t slot)
{
   switch (pool->query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
      return write_occlusion_query_result(device, pool, query, do_64bit,
                                          data, slot);
   case VK_QUERY_TYPE_TIMESTAMP:
      return write_timestamp_query_result(device, pool, query, do_64bit,
                                          data, slot);
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR:
      return write_performance_query_result(device, pool, query, do_64bit,
                                            data, slot);
   default:
      unreachable("Unsupported query type");
   }
}

static VkResult
query_is_available(struct v3dv_device *device,
                   struct v3dv_query_pool *pool,
                   uint32_t query,
                   bool do_wait,
                   bool *available)
{
   struct v3dv_query *q = &pool->queries[query];

   assert(pool->query_type != VK_QUERY_TYPE_OCCLUSION ||
          (q->bo && q->bo->map));

   if (do_wait) {
      VkResult result = query_wait_available(device, q, pool->query_type);
      if (result != VK_SUCCESS) {
         *available = false;
         return result;
      }

      *available = true;
   } else {
      VkResult result = query_check_available(device, q, pool->query_type);
      assert(result == VK_SUCCESS || result == VK_NOT_READY);
      *available = (result == VK_SUCCESS);
   }

   return VK_SUCCESS;
}

static uint32_t
get_query_result_count(struct v3dv_query_pool *pool)
{
   switch (pool->query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
      return 1;
   case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR:
      return pool->perfmon.ncounters;
   default:
      unreachable("Unsupported query type");
   }
}

VkResult
v3dv_get_query_pool_results(struct v3dv_device *device,
                            struct v3dv_query_pool *pool,
                            uint32_t first,
                            uint32_t count,
                            void *data,
                            VkDeviceSize stride,
                            VkQueryResultFlags flags)
{
   assert(first < pool->query_count);
   assert(first + count <= pool->query_count);
   assert(data);

   const bool do_64bit = flags & VK_QUERY_RESULT_64_BIT ||
      pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR;
   const bool do_wait = flags & VK_QUERY_RESULT_WAIT_BIT;
   const bool do_partial = flags & VK_QUERY_RESULT_PARTIAL_BIT;

   uint32_t result_count = get_query_result_count(pool);

   VkResult result = VK_SUCCESS;
   for (uint32_t i = first; i < first + count; i++) {
      bool available = false;
      VkResult query_result =
         query_is_available(device, pool, i, do_wait, &available);
      if (query_result == VK_ERROR_DEVICE_LOST)
         result = VK_ERROR_DEVICE_LOST;

      /**
       * From the Vulkan 1.0 spec:
       *
       *    "If VK_QUERY_RESULT_WAIT_BIT and VK_QUERY_RESULT_PARTIAL_BIT are
       *     both not set then no result values are written to pData for queries
       *     that are in the unavailable state at the time of the call, and
       *     vkGetQueryPoolResults returns VK_NOT_READY. However, availability
       *     state is still written to pData for those queries if
       *     VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set."
       */
      uint32_t slot = 0;

      const bool write_result = available || do_partial;
      if (write_result)
         write_query_result(device, pool, i, do_64bit, data, slot);
      slot += result_count;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         write_to_buffer(data, slot++, do_64bit, available ? 1u : 0u);

      if (!write_result && result != VK_ERROR_DEVICE_LOST)
         result = VK_NOT_READY;

      data += stride;
   }

   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetQueryPoolResults(VkDevice _device,
                         VkQueryPool queryPool,
                         uint32_t firstQuery,
                         uint32_t queryCount,
                         size_t dataSize,
                         void *pData,
                         VkDeviceSize stride,
                         VkQueryResultFlags flags)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);

   return v3dv_get_query_pool_results(device, pool, firstQuery, queryCount,
                                      pData, stride, flags);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_CmdResetQueryPool(VkCommandBuffer commandBuffer,
                       VkQueryPool queryPool,
                       uint32_t firstQuery,
                       uint32_t queryCount)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);

   v3dv_cmd_buffer_reset_queries(cmd_buffer, pool, firstQuery, queryCount);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                             VkQueryPool queryPool,
                             uint32_t firstQuery,
                             uint32_t queryCount,
                             VkBuffer dstBuffer,
                             VkDeviceSize dstOffset,
                             VkDeviceSize stride,
                             VkQueryResultFlags flags)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);
   V3DV_FROM_HANDLE(v3dv_buffer, dst, dstBuffer);

   v3dv_cmd_buffer_copy_query_results(cmd_buffer, pool,
                                      firstQuery, queryCount,
                                      dst, dstOffset, stride, flags);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_CmdBeginQuery(VkCommandBuffer commandBuffer,
                   VkQueryPool queryPool,
                   uint32_t query,
                   VkQueryControlFlags flags)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);

   v3dv_cmd_buffer_begin_query(cmd_buffer, pool, query, flags);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_CmdEndQuery(VkCommandBuffer commandBuffer,
                 VkQueryPool queryPool,
                 uint32_t query)
{
   V3DV_FROM_HANDLE(v3dv_cmd_buffer, cmd_buffer, commandBuffer);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);

   v3dv_cmd_buffer_end_query(cmd_buffer, pool, query);
}

void
v3dv_reset_query_pools(struct v3dv_device *device,
                       struct v3dv_query_pool *pool,
                       uint32_t first,
                       uint32_t count)
{
   mtx_lock(&device->query_mutex);

   for (uint32_t i = first; i < first + count; i++) {
      assert(i < pool->query_count);
      struct v3dv_query *q = &pool->queries[i];
      q->maybe_available = false;
      switch (pool->query_type) {
      case VK_QUERY_TYPE_OCCLUSION: {
         const uint8_t *q_addr = ((uint8_t *) q->bo->map) + q->offset;
         uint32_t *counter = (uint32_t *) q_addr;
         *counter = 0;
         break;
      }
      case VK_QUERY_TYPE_TIMESTAMP:
         q->value = 0;
         break;
      case VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR:
         kperfmon_destroy(device, pool, i);
         kperfmon_create(device, pool, i);
         if (vk_sync_reset(&device->vk, q->perf.last_job_sync) != VK_SUCCESS)
            fprintf(stderr, "Failed to reset sync");
         break;
      default:
         unreachable("Unsupported query type");
      }
   }

   mtx_unlock(&device->query_mutex);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_ResetQueryPool(VkDevice _device,
                    VkQueryPool queryPool,
                    uint32_t firstQuery,
                    uint32_t queryCount)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_query_pool, pool, queryPool);

   v3dv_reset_query_pools(device, pool, firstQuery, queryCount);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_EnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR(
   VkPhysicalDevice physicalDevice,
   uint32_t queueFamilyIndex,
   uint32_t *pCounterCount,
   VkPerformanceCounterKHR *pCounters,
   VkPerformanceCounterDescriptionKHR *pCounterDescriptions)
{
   uint32_t desc_count = *pCounterCount;

   VK_OUTARRAY_MAKE_TYPED(VkPerformanceCounterKHR,
                          out, pCounters, pCounterCount);
   VK_OUTARRAY_MAKE_TYPED(VkPerformanceCounterDescriptionKHR,
                          out_desc, pCounterDescriptions, &desc_count);

   for (int i = 0; i < ARRAY_SIZE(v3dv_counters); i++) {
      vk_outarray_append_typed(VkPerformanceCounterKHR, &out, counter) {
         counter->unit = VK_PERFORMANCE_COUNTER_UNIT_GENERIC_KHR;
         counter->scope = VK_PERFORMANCE_COUNTER_SCOPE_COMMAND_KHR;
         counter->storage = VK_PERFORMANCE_COUNTER_STORAGE_UINT64_KHR;

         unsigned char sha1_result[20];
         _mesa_sha1_compute(v3dv_counters[i][1], strlen(v3dv_counters[i][1]),
                            sha1_result);

         memcpy(counter->uuid, sha1_result, sizeof(counter->uuid));
      }

      vk_outarray_append_typed(VkPerformanceCounterDescriptionKHR,
                               &out_desc, desc) {
         desc->flags = 0;
         snprintf(desc->name, sizeof(desc->name), "%s",
            v3dv_counters[i][1]);
         snprintf(desc->category, sizeof(desc->category), "%s",
            v3dv_counters[i][0]);
         snprintf(desc->description, sizeof(desc->description), "%s",
            v3dv_counters[i][2]);
      }
   }

   return vk_outarray_status(&out);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR(
   VkPhysicalDevice physicalDevice,
   const VkQueryPoolPerformanceCreateInfoKHR *pPerformanceQueryCreateInfo,
   uint32_t *pNumPasses)
{
   *pNumPasses = DIV_ROUND_UP(pPerformanceQueryCreateInfo->counterIndexCount,
                              DRM_V3D_MAX_PERF_COUNTERS);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_AcquireProfilingLockKHR(
   VkDevice _device,
   const VkAcquireProfilingLockInfoKHR *pInfo)
{
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_ReleaseProfilingLockKHR(VkDevice device)
{
}
