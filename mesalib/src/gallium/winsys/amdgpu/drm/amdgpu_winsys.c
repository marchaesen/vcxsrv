/*
 * Copyright © 2009 Corbin Simpson <MostAwesomeDude@gmail.com>
 * Copyright © 2009 Joakim Sindholt <opensource@zhasha.com>
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include "amdgpu_cs.h"
#include "amdgpu_public.h"

#include "util/os_file.h"
#include "util/os_misc.h"
#include "util/u_cpu_detect.h"
#include "util/u_hash_table.h"
#include "util/hash_table.h"
#include "util/xmlconfig.h"
#include "drm-uapi/amdgpu_drm.h"
#include <xf86drm.h>
#include <stdio.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "ac_llvm_util.h"
#include "sid.h"

static struct hash_table *dev_tab = NULL;
static simple_mtx_t dev_tab_mutex = _SIMPLE_MTX_INITIALIZER_NP;

#if DEBUG
DEBUG_GET_ONCE_BOOL_OPTION(all_bos, "RADEON_ALL_BOS", false)
#endif

static void handle_env_var_force_family(struct amdgpu_winsys *ws)
{
      const char *family = debug_get_option("SI_FORCE_FAMILY", NULL);
      unsigned i;

      if (!family)
               return;

      for (i = CHIP_TAHITI; i < CHIP_LAST; i++) {
         if (!strcmp(family, ac_get_llvm_processor_name(i))) {
            /* Override family and chip_class. */
            ws->info.family = i;
            ws->info.name = "GCN-NOOP";

            if (i >= CHIP_SIENNA_CICHLID)
               ws->info.chip_class = GFX10_3;
            else if (i >= CHIP_NAVI10)
               ws->info.chip_class = GFX10;
            else if (i >= CHIP_VEGA10)
               ws->info.chip_class = GFX9;
            else if (i >= CHIP_TONGA)
               ws->info.chip_class = GFX8;
            else if (i >= CHIP_BONAIRE)
               ws->info.chip_class = GFX7;
            else
               ws->info.chip_class = GFX6;

            /* Don't submit any IBs. */
            setenv("RADEON_NOOP", "1", 1);
            return;
         }
      }

      fprintf(stderr, "radeonsi: Unknown family: %s\n", family);
      exit(1);
}

/* Helper function to do the ioctls needed for setup and init. */
static bool do_winsys_init(struct amdgpu_winsys *ws,
                           const struct pipe_screen_config *config,
                           int fd)
{
   if (!ac_query_gpu_info(fd, ws->dev, &ws->info, &ws->amdinfo))
      goto fail;

   /* TODO: Enable this once the kernel handles it efficiently. */
   if (ws->info.has_dedicated_vram)
      ws->info.has_local_buffers = false;

   handle_env_var_force_family(ws);

   ws->addrlib = ac_addrlib_create(&ws->info, &ws->info.max_alignment);
   if (!ws->addrlib) {
      fprintf(stderr, "amdgpu: Cannot create addrlib.\n");
      goto fail;
   }

   ws->check_vm = strstr(debug_get_option("R600_DEBUG", ""), "check_vm") != NULL ||
                  strstr(debug_get_option("AMD_DEBUG", ""), "check_vm") != NULL;
   ws->noop_cs = debug_get_bool_option("RADEON_NOOP", false);
#if DEBUG
   ws->debug_all_bos = debug_get_option_all_bos();
#endif
   ws->reserve_vmid = strstr(debug_get_option("R600_DEBUG", ""), "reserve_vmid") != NULL ||
                      strstr(debug_get_option("AMD_DEBUG", ""), "reserve_vmid") != NULL;
   ws->zero_all_vram_allocs = strstr(debug_get_option("R600_DEBUG", ""), "zerovram") != NULL ||
                              driQueryOptionb(config->options, "radeonsi_zerovram");

   return true;

fail:
   amdgpu_device_deinitialize(ws->dev);
   ws->dev = NULL;
   return false;
}

static void do_winsys_deinit(struct amdgpu_winsys *ws)
{
   if (ws->reserve_vmid)
      amdgpu_vm_unreserve_vmid(ws->dev, 0);

   if (util_queue_is_initialized(&ws->cs_queue))
      util_queue_destroy(&ws->cs_queue);

   simple_mtx_destroy(&ws->bo_fence_lock);
   for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
      if (ws->bo_slabs[i].groups)
         pb_slabs_deinit(&ws->bo_slabs[i]);
   }
   pb_cache_deinit(&ws->bo_cache);
   _mesa_hash_table_destroy(ws->bo_export_table, NULL);
   simple_mtx_destroy(&ws->sws_list_lock);
#if DEBUG
   simple_mtx_destroy(&ws->global_bo_list_lock);
#endif
   simple_mtx_destroy(&ws->bo_export_table_lock);

   ac_addrlib_destroy(ws->addrlib);
   amdgpu_device_deinitialize(ws->dev);
   FREE(ws);
}

static void amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
   struct amdgpu_screen_winsys *sws = amdgpu_screen_winsys(rws);
   struct amdgpu_winsys *ws = sws->aws;
   bool destroy;

   /* When the reference counter drops to zero, remove the device pointer
    * from the table.
    * This must happen while the mutex is locked, so that
    * amdgpu_winsys_create in another thread doesn't get the winsys
    * from the table when the counter drops to 0.
    */
   simple_mtx_lock(&dev_tab_mutex);

   destroy = pipe_reference(&ws->reference, NULL);
   if (destroy && dev_tab) {
      _mesa_hash_table_remove_key(dev_tab, ws->dev);
      if (_mesa_hash_table_num_entries(dev_tab) == 0) {
         _mesa_hash_table_destroy(dev_tab, NULL);
         dev_tab = NULL;
      }
   }

   simple_mtx_unlock(&dev_tab_mutex);

   if (destroy)
      do_winsys_deinit(ws);

   close(sws->fd);
   FREE(rws);
}

static void amdgpu_winsys_query_info(struct radeon_winsys *rws,
                                     struct radeon_info *info,
                                     bool enable_smart_access_memory,
                                     bool disable_smart_access_memory)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);

   if (disable_smart_access_memory)
      ws->info.smart_access_memory = false;
   else if (enable_smart_access_memory && ws->info.all_vram_visible)
      ws->info.smart_access_memory = true;

   *info = ws->info;
}

static bool amdgpu_cs_request_feature(struct radeon_cmdbuf *rcs,
                                      enum radeon_feature_id fid,
                                      bool enable)
{
   return false;
}

static uint64_t amdgpu_query_value(struct radeon_winsys *rws,
                                   enum radeon_value_id value)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);
   struct amdgpu_heap_info heap;
   uint64_t retval = 0;

   switch (value) {
   case RADEON_REQUESTED_VRAM_MEMORY:
      return ws->allocated_vram;
   case RADEON_REQUESTED_GTT_MEMORY:
      return ws->allocated_gtt;
   case RADEON_MAPPED_VRAM:
      return ws->mapped_vram;
   case RADEON_MAPPED_GTT:
      return ws->mapped_gtt;
   case RADEON_SLAB_WASTED_VRAM:
      return ws->slab_wasted_vram;
   case RADEON_SLAB_WASTED_GTT:
      return ws->slab_wasted_gtt;
   case RADEON_BUFFER_WAIT_TIME_NS:
      return ws->buffer_wait_time;
   case RADEON_NUM_MAPPED_BUFFERS:
      return ws->num_mapped_buffers;
   case RADEON_TIMESTAMP:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_TIMESTAMP, 8, &retval);
      return retval;
   case RADEON_NUM_GFX_IBS:
      return ws->num_gfx_IBs;
   case RADEON_NUM_SDMA_IBS:
      return ws->num_sdma_IBs;
   case RADEON_GFX_BO_LIST_COUNTER:
      return ws->gfx_bo_list_counter;
   case RADEON_GFX_IB_SIZE_COUNTER:
      return ws->gfx_ib_size_counter;
   case RADEON_NUM_BYTES_MOVED:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_BYTES_MOVED, 8, &retval);
      return retval;
   case RADEON_NUM_EVICTIONS:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_EVICTIONS, 8, &retval);
      return retval;
   case RADEON_NUM_VRAM_CPU_PAGE_FAULTS:
      amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_VRAM_CPU_PAGE_FAULTS, 8, &retval);
      return retval;
   case RADEON_VRAM_USAGE:
      amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &heap);
      return heap.heap_usage;
   case RADEON_VRAM_VIS_USAGE:
      amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM,
                             AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, &heap);
      return heap.heap_usage;
   case RADEON_GTT_USAGE:
      amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &heap);
      return heap.heap_usage;
   case RADEON_GPU_TEMPERATURE:
      amdgpu_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GPU_TEMP, 4, &retval);
      return retval;
   case RADEON_CURRENT_SCLK:
      amdgpu_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_SCLK, 4, &retval);
      return retval;
   case RADEON_CURRENT_MCLK:
      amdgpu_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_MCLK, 4, &retval);
      return retval;
   case RADEON_CS_THREAD_TIME:
      return util_queue_get_thread_time_nano(&ws->cs_queue, 0);
   }
   return 0;
}

static bool amdgpu_read_registers(struct radeon_winsys *rws,
                                  unsigned reg_offset,
                                  unsigned num_registers, uint32_t *out)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);

   return amdgpu_read_mm_registers(ws->dev, reg_offset / 4, num_registers,
                                   0xffffffff, 0, out) == 0;
}

static bool amdgpu_winsys_unref(struct radeon_winsys *rws)
{
   struct amdgpu_screen_winsys *sws = amdgpu_screen_winsys(rws);
   struct amdgpu_winsys *aws = sws->aws;
   bool ret;

   simple_mtx_lock(&aws->sws_list_lock);

   ret = pipe_reference(&sws->reference, NULL);
   if (ret) {
      struct amdgpu_screen_winsys **sws_iter;
      struct amdgpu_winsys *aws = sws->aws;

      /* Remove this amdgpu_screen_winsys from amdgpu_winsys' list, so that
       * amdgpu_winsys_create can't re-use it anymore
       */
      for (sws_iter = &aws->sws_list; *sws_iter; sws_iter = &(*sws_iter)->next) {
         if (*sws_iter == sws) {
            *sws_iter = sws->next;
            break;
         }
      }
   }

   simple_mtx_unlock(&aws->sws_list_lock);

   if (ret && sws->kms_handles) {
      struct drm_gem_close args;

      hash_table_foreach(sws->kms_handles, entry) {
         args.handle = (uintptr_t)entry->data;
         drmIoctl(sws->fd, DRM_IOCTL_GEM_CLOSE, &args);
      }
      _mesa_hash_table_destroy(sws->kms_handles, NULL);
   }

   return ret;
}

static void amdgpu_pin_threads_to_L3_cache(struct radeon_winsys *rws,
                                           unsigned cache)
{
   struct amdgpu_winsys *ws = amdgpu_winsys(rws);

   util_set_thread_affinity(ws->cs_queue.threads[0],
                            util_get_cpu_caps()->L3_affinity_mask[cache],
                            NULL, util_get_cpu_caps()->num_cpu_mask_bits);
}

static uint32_t kms_handle_hash(const void *key)
{
   const struct amdgpu_winsys_bo *bo = key;

   return bo->u.real.kms_handle;
}

static bool kms_handle_equals(const void *a, const void *b)
{
   return a == b;
}

static bool amdgpu_cs_is_secure(struct radeon_cmdbuf *rcs)
{
   struct amdgpu_cs *cs = amdgpu_cs(rcs);
   return cs->csc->secure;
}

PUBLIC struct radeon_winsys *
amdgpu_winsys_create(int fd, const struct pipe_screen_config *config,
		     radeon_screen_create_t screen_create)
{
   struct amdgpu_screen_winsys *ws;
   struct amdgpu_winsys *aws;
   amdgpu_device_handle dev;
   uint32_t drm_major, drm_minor;
   int r;

   ws = CALLOC_STRUCT(amdgpu_screen_winsys);
   if (!ws)
      return NULL;

   pipe_reference_init(&ws->reference, 1);
   ws->fd = os_dupfd_cloexec(fd);

   /* Look up the winsys from the dev table. */
   simple_mtx_lock(&dev_tab_mutex);
   if (!dev_tab)
      dev_tab = util_hash_table_create_ptr_keys();

   /* Initialize the amdgpu device. This should always return the same pointer
    * for the same fd. */
   r = amdgpu_device_initialize(ws->fd, &drm_major, &drm_minor, &dev);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_device_initialize failed.\n");
      goto fail;
   }

   /* Lookup a winsys if we have already created one for this device. */
   aws = util_hash_table_get(dev_tab, dev);
   if (aws) {
      struct amdgpu_screen_winsys *sws_iter;

      /* Release the device handle, because we don't need it anymore.
       * This function is returning an existing winsys instance, which
       * has its own device handle.
       */
      amdgpu_device_deinitialize(dev);

      simple_mtx_lock(&aws->sws_list_lock);
      for (sws_iter = aws->sws_list; sws_iter; sws_iter = sws_iter->next) {
         r = os_same_file_description(sws_iter->fd, ws->fd);

         if (r == 0) {
            close(ws->fd);
            FREE(ws);
            ws = sws_iter;
            pipe_reference(NULL, &ws->reference);
            simple_mtx_unlock(&aws->sws_list_lock);
            goto unlock;
         } else if (r < 0) {
            static bool logged;

            if (!logged) {
               os_log_message("amdgpu: os_same_file_description couldn't "
                              "determine if two DRM fds reference the same "
                              "file description.\n"
                              "If they do, bad things may happen!\n");
               logged = true;
            }
         }
      }
      simple_mtx_unlock(&aws->sws_list_lock);

      ws->kms_handles = _mesa_hash_table_create(NULL, kms_handle_hash,
                                                kms_handle_equals);
      if (!ws->kms_handles)
         goto fail;

      pipe_reference(NULL, &aws->reference);
   } else {
      /* Create a new winsys. */
      aws = CALLOC_STRUCT(amdgpu_winsys);
      if (!aws)
         goto fail;

      aws->dev = dev;
      aws->fd = ws->fd;
      aws->info.drm_major = drm_major;
      aws->info.drm_minor = drm_minor;
      aws->dummy_ws.aws = aws; /* only the pointer is used */

      if (!do_winsys_init(aws, config, fd))
         goto fail_alloc;

      /* Create managers. */
      pb_cache_init(&aws->bo_cache, RADEON_MAX_CACHED_HEAPS,
                    500000, aws->check_vm ? 1.0f : 2.0f, 0,
                    (aws->info.vram_size + aws->info.gart_size) / 8, aws,
                    /* Cast to void* because one of the function parameters
                     * is a struct pointer instead of void*. */
                    (void*)amdgpu_bo_destroy, (void*)amdgpu_bo_can_reclaim);

      unsigned min_slab_order = 8;  /* 256 bytes */
      unsigned max_slab_order = 20; /* 1 MB (slab size = 2 MB) */
      unsigned num_slab_orders_per_allocator = (max_slab_order - min_slab_order) /
                                               NUM_SLAB_ALLOCATORS;

      /* Divide the size order range among slab managers. */
      for (unsigned i = 0; i < NUM_SLAB_ALLOCATORS; i++) {
         unsigned min_order = min_slab_order;
         unsigned max_order = MIN2(min_order + num_slab_orders_per_allocator,
                                   max_slab_order);

         if (!pb_slabs_init(&aws->bo_slabs[i],
                            min_order, max_order,
                            RADEON_MAX_SLAB_HEAPS, true,
                            aws,
                            amdgpu_bo_can_reclaim_slab,
                            amdgpu_bo_slab_alloc_normal,
                            /* Cast to void* because one of the function parameters
                             * is a struct pointer instead of void*. */
                            (void*)amdgpu_bo_slab_free)) {
            amdgpu_winsys_destroy(&ws->base);
            simple_mtx_unlock(&dev_tab_mutex);
            return NULL;
         }

         if (aws->info.has_tmz_support &&
             !pb_slabs_init(&aws->bo_slabs_encrypted[i],
                            min_order, max_order,
                            RADEON_MAX_SLAB_HEAPS, true,
                            aws,
                            amdgpu_bo_can_reclaim_slab,
                            amdgpu_bo_slab_alloc_encrypted,
                            /* Cast to void* because one of the function parameters
                             * is a struct pointer instead of void*. */
                            (void*)amdgpu_bo_slab_free)) {
            amdgpu_winsys_destroy(&ws->base);
            simple_mtx_unlock(&dev_tab_mutex);
            return NULL;
         }

         min_slab_order = max_order + 1;
      }

      aws->info.min_alloc_size = 1 << aws->bo_slabs[0].min_order;

      /* init reference */
      pipe_reference_init(&aws->reference, 1);
#if DEBUG
      list_inithead(&aws->global_bo_list);
#endif
      aws->bo_export_table = util_hash_table_create_ptr_keys();

      (void) simple_mtx_init(&aws->sws_list_lock, mtx_plain);
#if DEBUG
      (void) simple_mtx_init(&aws->global_bo_list_lock, mtx_plain);
#endif
      (void) simple_mtx_init(&aws->bo_fence_lock, mtx_plain);
      (void) simple_mtx_init(&aws->bo_export_table_lock, mtx_plain);

      if (!util_queue_init(&aws->cs_queue, "cs", 8, 1,
                           UTIL_QUEUE_INIT_RESIZE_IF_FULL, NULL)) {
         amdgpu_winsys_destroy(&ws->base);
         simple_mtx_unlock(&dev_tab_mutex);
         return NULL;
      }

      _mesa_hash_table_insert(dev_tab, dev, aws);

      if (aws->reserve_vmid) {
         r = amdgpu_vm_reserve_vmid(dev, 0);
         if (r) {
            amdgpu_winsys_destroy(&ws->base);
            simple_mtx_unlock(&dev_tab_mutex);
            return NULL;
         }
      }
   }

   ws->aws = aws;

   /* Set functions. */
   ws->base.unref = amdgpu_winsys_unref;
   ws->base.destroy = amdgpu_winsys_destroy;
   ws->base.query_info = amdgpu_winsys_query_info;
   ws->base.cs_request_feature = amdgpu_cs_request_feature;
   ws->base.query_value = amdgpu_query_value;
   ws->base.read_registers = amdgpu_read_registers;
   ws->base.pin_threads_to_L3_cache = amdgpu_pin_threads_to_L3_cache;
   ws->base.cs_is_secure = amdgpu_cs_is_secure;

   amdgpu_bo_init_functions(ws);
   amdgpu_cs_init_functions(ws);
   amdgpu_surface_init_functions(ws);

   simple_mtx_lock(&aws->sws_list_lock);
   ws->next = aws->sws_list;
   aws->sws_list = ws;
   simple_mtx_unlock(&aws->sws_list_lock);

   /* Create the screen at the end. The winsys must be initialized
    * completely.
    *
    * Alternatively, we could create the screen based on "ws->gen"
    * and link all drivers into one binary blob. */
   ws->base.screen = screen_create(&ws->base, config);
   if (!ws->base.screen) {
      amdgpu_winsys_destroy(&ws->base);
      simple_mtx_unlock(&dev_tab_mutex);
      return NULL;
   }

unlock:
   /* We must unlock the mutex once the winsys is fully initialized, so that
    * other threads attempting to create the winsys from the same fd will
    * get a fully initialized winsys and not just half-way initialized. */
   simple_mtx_unlock(&dev_tab_mutex);

   return &ws->base;

fail_alloc:
   FREE(aws);
fail:
   if (ws->kms_handles)
      _mesa_hash_table_destroy(ws->kms_handles, NULL);
   close(ws->fd);
   FREE(ws);
   simple_mtx_unlock(&dev_tab_mutex);
   return NULL;
}
