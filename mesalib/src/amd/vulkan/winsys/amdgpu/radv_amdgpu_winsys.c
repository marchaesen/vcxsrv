/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "radv_amdgpu_winsys.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "drm-uapi/amdgpu_drm.h"
#include "ac_linux_drm.h"
#include "ac_surface.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_winsys_public.h"
#include "radv_debug.h"
#include "vk_drm_syncobj.h"
#include "xf86drm.h"

static bool
radv_is_gpu_supported(const struct radeon_info *info)
{
   /* AMD CDNA isn't supported. */
   if (info->gfx_level == GFX9 && !info->has_graphics)
      return false;

   /* Unknown GPU generations aren't supported. */
   if (info->gfx_level > GFX12)
      return false;

   return true;
}

static bool
do_winsys_init(struct radv_amdgpu_winsys *ws, int fd)
{
   if (!ac_query_gpu_info(fd, ws->dev, &ws->info, true))
      return false;

   if (!radv_is_gpu_supported(&ws->info))
      return false;

   /*
    * Override the max submits on video queues.
    * If you submit multiple session contexts in the same IB sequence the
    * hardware gets upset as it expects a kernel fence to be emitted to reset
    * the session context in the hardware.
    * Avoid this problem by never submitted more than one IB at a time.
    * This possibly should be fixed in the kernel, and if it is this can be
    * resolved.
    */
   for (enum amd_ip_type ip_type = AMD_IP_UVD; ip_type <= AMD_IP_VCN_ENC; ip_type++)
      ws->info.max_submitted_ibs[ip_type] = 1;

   ws->info.ip[AMD_IP_SDMA].num_queues = MIN2(ws->info.ip[AMD_IP_SDMA].num_queues, MAX_RINGS_PER_TYPE);
   ws->info.ip[AMD_IP_COMPUTE].num_queues = MIN2(ws->info.ip[AMD_IP_COMPUTE].num_queues, MAX_RINGS_PER_TYPE);

   ws->use_ib_bos = true;
   return true;
}

static void
radv_amdgpu_winsys_query_info(struct radeon_winsys *rws, struct radeon_info *gpu_info)
{
   *gpu_info = ((struct radv_amdgpu_winsys *)rws)->info;
}

static uint64_t
radv_amdgpu_winsys_query_value(struct radeon_winsys *rws, enum radeon_value_id value)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   struct amdgpu_heap_info heap = {0};
   uint64_t retval = 0;

   switch (value) {
   case RADEON_ALLOCATED_VRAM:
      return ws->allocated_vram;
   case RADEON_ALLOCATED_VRAM_VIS:
      return ws->allocated_vram_vis;
   case RADEON_ALLOCATED_GTT:
      return ws->allocated_gtt;
   case RADEON_TIMESTAMP:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_TIMESTAMP, 8, &retval);
      return retval;
   case RADEON_NUM_BYTES_MOVED:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_NUM_BYTES_MOVED, 8, &retval);
      return retval;
   case RADEON_NUM_EVICTIONS:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_NUM_EVICTIONS, 8, &retval);
      return retval;
   case RADEON_NUM_VRAM_CPU_PAGE_FAULTS:
      ac_drm_query_info(ws->dev, AMDGPU_INFO_NUM_VRAM_CPU_PAGE_FAULTS, 8, &retval);
      return retval;
   case RADEON_VRAM_USAGE:
      ac_drm_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &heap);
      return heap.heap_usage;
   case RADEON_VRAM_VIS_USAGE:
      ac_drm_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, &heap);
      return heap.heap_usage;
   case RADEON_GTT_USAGE:
      ac_drm_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &heap);
      return heap.heap_usage;
   case RADEON_GPU_TEMPERATURE:
      ac_drm_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, 4, &retval);
      return retval;
   case RADEON_CURRENT_SCLK:
      ac_drm_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, 4, &retval);
      return retval;
   case RADEON_CURRENT_MCLK:
      ac_drm_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, 4, &retval);
      return retval;
   default:
      unreachable("invalid query value");
   }

   return 0;
}

static bool
radv_amdgpu_winsys_read_registers(struct radeon_winsys *rws, unsigned reg_offset, unsigned num_registers, uint32_t *out)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;

   return ac_drm_read_mm_registers(ws->dev, reg_offset / 4, num_registers, 0xffffffff, 0, out) == 0;
}

static const char *
radv_amdgpu_winsys_get_chip_name(struct radeon_winsys *rws)
{
   ac_drm_device *dev = ((struct radv_amdgpu_winsys *)rws)->dev;

   return ac_drm_get_marketing_name(dev);
}

static bool
radv_amdgpu_winsys_query_gpuvm_fault(struct radeon_winsys *rws, struct radv_winsys_gpuvm_fault_info *fault_info)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   struct drm_amdgpu_info_gpuvm_fault gpuvm_fault = {0};
   int r;

   r = ac_drm_query_info(ws->dev, AMDGPU_INFO_GPUVM_FAULT, sizeof(gpuvm_fault), &gpuvm_fault);
   if (r < 0) {
      fprintf(stderr, "radv/amdgpu: Failed to query the last GPUVM fault (%d).\n", r);
      return false;
   }

   /* When the GPUVM fault status is 0, no faults happened. */
   if (!gpuvm_fault.status)
      return false;

   fault_info->addr = gpuvm_fault.addr;
   fault_info->status = gpuvm_fault.status;
   fault_info->vmhub = gpuvm_fault.vmhub;

   return true;
}

static simple_mtx_t winsys_creation_mutex = SIMPLE_MTX_INITIALIZER;
static struct hash_table *winsyses = NULL;

static void
radv_amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   bool destroy = false;

   simple_mtx_lock(&winsys_creation_mutex);
   if (!--ws->refcount) {
      _mesa_hash_table_remove_key(winsyses, ws->dev);

      /* Clean the hashtable up if empty, though there is no
       * empty function. */
      if (_mesa_hash_table_num_entries(winsyses) == 0) {
         _mesa_hash_table_destroy(winsyses, NULL);
         winsyses = NULL;
      }

      destroy = true;
   }
   simple_mtx_unlock(&winsys_creation_mutex);
   if (!destroy)
      return;

   u_rwlock_destroy(&ws->global_bo_list.lock);
   free(ws->global_bo_list.bos);

   if (ws->reserve_vmid)
      ac_drm_vm_unreserve_vmid(ws->dev, 0);

   u_rwlock_destroy(&ws->log_bo_list_lock);
   ac_drm_device_deinitialize(ws->dev);
   FREE(rws);
}

static int
radv_amdgpu_winsys_get_fd(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   return ws->fd;
}

static const struct vk_sync_type *const *
radv_amdgpu_winsys_get_sync_types(struct radeon_winsys *rws)
{
   struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
   return ws->sync_types;
}

struct radeon_winsys *
radv_amdgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags, bool reserve_vmid, bool is_virtio)
{
   uint32_t drm_major, drm_minor, r;
   ac_drm_device *dev;
   struct radv_amdgpu_winsys *ws = NULL;

   r = ac_drm_device_initialize(fd, is_virtio, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "radv/amdgpu: failed to initialize device.\n");
      return NULL;
   }

   /* We have to keep this lock till insertion. */
   simple_mtx_lock(&winsys_creation_mutex);
   if (!winsyses)
      winsyses = _mesa_pointer_hash_table_create(NULL);
   if (!winsyses) {
      fprintf(stderr, "radv/amdgpu: failed to alloc winsys hash table.\n");
      goto fail;
   }

   struct hash_entry *entry = _mesa_hash_table_search(winsyses, dev);
   if (entry) {
      ws = (struct radv_amdgpu_winsys *)entry->data;
      ++ws->refcount;
   }

   if (ws) {
      simple_mtx_unlock(&winsys_creation_mutex);
      ac_drm_device_deinitialize(dev);

      /* Check that options don't differ from the existing winsys. */
      if (((debug_flags & RADV_DEBUG_ALL_BOS) && !ws->debug_all_bos) ||
          ((debug_flags & RADV_DEBUG_HANG) && !ws->debug_log_bos) ||
          ((debug_flags & RADV_DEBUG_NO_IBS) && ws->use_ib_bos) || (perftest_flags != ws->perftest)) {
         fprintf(stderr, "radv/amdgpu: Found options that differ from the existing winsys.\n");
         return NULL;
      }

      /* RADV_DEBUG_ZERO_VRAM is the only option that is allowed to be set again. */
      if (debug_flags & RADV_DEBUG_ZERO_VRAM)
         ws->zero_all_vram_allocs = true;

      return &ws->base;
   }

   ws = calloc(1, sizeof(struct radv_amdgpu_winsys));
   if (!ws)
      goto fail;

   ws->refcount = 1;
   ws->dev = dev;
   ws->fd = ac_drm_device_get_fd(dev);
   ws->info.drm_major = drm_major;
   ws->info.drm_minor = drm_minor;
   ws->info.is_virtio = is_virtio;
   if (!do_winsys_init(ws, fd))
      goto winsys_fail;

   ws->debug_all_bos = !!(debug_flags & RADV_DEBUG_ALL_BOS);
   ws->debug_log_bos = debug_flags & RADV_DEBUG_HANG;
   if (debug_flags & RADV_DEBUG_NO_IBS)
      ws->use_ib_bos = false;

   ws->reserve_vmid = reserve_vmid;
   if (ws->reserve_vmid) {
      r = ac_drm_vm_reserve_vmid(ws->dev, 0);
      if (r) {
         fprintf(stderr, "radv/amdgpu: failed to reserve vmid.\n");
         goto winsys_fail;
      }
   }
   int num_sync_types = 0;

   ws->syncobj_sync_type = vk_drm_syncobj_get_type(ws->fd);
   if (ws->syncobj_sync_type.features) {
      /* multi wait is always supported */
      ws->syncobj_sync_type.features |= VK_SYNC_FEATURE_GPU_MULTI_WAIT;

      if (!ws->info.has_timeline_syncobj && ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE) {
         /* Disable timeline feature if it was disabled in the driver. */
         assert(is_virtio);
         ws->syncobj_sync_type.get_value = NULL;
         ws->syncobj_sync_type.features &= ~VK_SYNC_FEATURE_TIMELINE;
      }

      ws->sync_types[num_sync_types++] = &ws->syncobj_sync_type;
      if (!(ws->syncobj_sync_type.features & VK_SYNC_FEATURE_TIMELINE)) {
         ws->emulated_timeline_sync_type = vk_sync_timeline_get_type(&ws->syncobj_sync_type);
         ws->sync_types[num_sync_types++] = &ws->emulated_timeline_sync_type.sync;
      }
   }

   ws->sync_types[num_sync_types++] = NULL;
   assert(num_sync_types <= ARRAY_SIZE(ws->sync_types));

   ws->perftest = perftest_flags;
   ws->zero_all_vram_allocs = debug_flags & RADV_DEBUG_ZERO_VRAM;
   u_rwlock_init(&ws->global_bo_list.lock);
   list_inithead(&ws->log_bo_list);
   u_rwlock_init(&ws->log_bo_list_lock);
   ws->base.query_info = radv_amdgpu_winsys_query_info;
   ws->base.query_value = radv_amdgpu_winsys_query_value;
   ws->base.read_registers = radv_amdgpu_winsys_read_registers;
   ws->base.get_chip_name = radv_amdgpu_winsys_get_chip_name;
   ws->base.query_gpuvm_fault = radv_amdgpu_winsys_query_gpuvm_fault;
   ws->base.destroy = radv_amdgpu_winsys_destroy;
   ws->base.get_fd = radv_amdgpu_winsys_get_fd;
   ws->base.get_sync_types = radv_amdgpu_winsys_get_sync_types;
   radv_amdgpu_bo_init_functions(ws);
   radv_amdgpu_cs_init_functions(ws);

   _mesa_hash_table_insert(winsyses, dev, ws);
   simple_mtx_unlock(&winsys_creation_mutex);

   return &ws->base;

winsys_fail:
   free(ws);
fail:
   if (winsyses && _mesa_hash_table_num_entries(winsyses) == 0) {
      _mesa_hash_table_destroy(winsyses, NULL);
      winsyses = NULL;
   }
   simple_mtx_unlock(&winsys_creation_mutex);
   ac_drm_device_deinitialize(dev);
   return NULL;
}
