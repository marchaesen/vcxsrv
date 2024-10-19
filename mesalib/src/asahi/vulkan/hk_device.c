/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_device.h"

#include "agx_bg_eot.h"
#include "agx_helpers.h"
#include "agx_opcodes.h"
#include "agx_scratch.h"
#include "hk_cmd_buffer.h"
#include "hk_descriptor_table.h"
#include "hk_entrypoints.h"
#include "hk_instance.h"
#include "hk_physical_device.h"
#include "hk_shader.h"

#include "asahi/genxml/agx_pack.h"
#include "asahi/lib/agx_bo.h"
#include "asahi/lib/agx_device.h"
#include "asahi/lib/shaders/geometry.h"
#include "util/hash_table.h"
#include "util/os_file.h"
#include "util/ralloc.h"
#include "util/simple_mtx.h"
#include "vulkan/vulkan_core.h"
#include "vulkan/wsi/wsi_common.h"
#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_common_entrypoints.h"
#include "vk_pipeline_cache.h"

#include <fcntl.h>
#include <xf86drm.h>

/* clang-format off */
static const struct debug_named_value hk_perf_options[] = {
   {"notess",    HK_PERF_NOTESS,   "Skip draws with tessellation"},
   {"noborder",  HK_PERF_NOBORDER, "Disable custom border colour emulation"},
   {"nobarrier", HK_PERF_NOBARRIER,"Ignore pipeline barriers"},
   {"batch",     HK_PERF_BATCH,    "Batch submissions"},
   {"norobust",  HK_PERF_NOROBUST, "Disable robustness"},
   DEBUG_NAMED_VALUE_END
};
/* clang-format on */

/*
 * We preupload some constants so we can cheaply reference later without extra
 * allocation and copying.
 *
 * TODO: This is small, don't waste a whole BO.
 */
static VkResult
hk_upload_rodata(struct hk_device *dev)
{
   dev->rodata.bo =
      agx_bo_create(&dev->dev, AGX_SAMPLER_LENGTH, 0, 0, "Read only data");

   if (!dev->rodata.bo)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   uint8_t *map = dev->rodata.bo->map;
   uint32_t offs = 0;

   offs = align(offs, 8);
   agx_pack(&dev->rodata.txf_sampler, USC_SAMPLER, cfg) {
      cfg.start = 0;
      cfg.count = 1;
      cfg.buffer = dev->rodata.bo->va->addr + offs;
   }

   agx_pack_txf_sampler((struct agx_sampler_packed *)(map + offs));
   offs += AGX_SAMPLER_LENGTH;

   /* The image heap is allocated on the device prior to the rodata. The heap
    * lives as long as the device does and has a stable address (requiring
    * sparse binding to grow dynamically). That means its address is effectively
    * rodata and can be uploaded now. agx_usc_uniform requires an indirection to
    * push the heap address, so this takes care of that indirection up front to
    * cut an alloc/upload at draw time.
    */
   offs = align(offs, sizeof(uint64_t));
   agx_pack(&dev->rodata.image_heap, USC_UNIFORM, cfg) {
      cfg.start_halfs = HK_IMAGE_HEAP_UNIFORM;
      cfg.size_halfs = 4;
      cfg.buffer = dev->rodata.bo->va->addr + offs;
   }

   uint64_t *image_heap_ptr = dev->rodata.bo->map + offs;
   *image_heap_ptr = dev->images.bo->va->addr;
   offs += sizeof(uint64_t);

   /* The geometry state buffer isn't strictly readonly data, but we only have a
    * single instance of it device-wide and -- after initializing at heap
    * allocate time -- it is read-only from the CPU perspective. The GPU uses it
    * for scratch, but is required to reset it after use to ensure resubmitting
    * the same command buffer works.
    *
    * So, we allocate it here for convenience.
    */
   offs = align(offs, sizeof(uint64_t));
   dev->rodata.geometry_state = dev->rodata.bo->va->addr + offs;
   offs += sizeof(struct agx_geometry_state);

   /* For null readonly buffers, we need to allocate 16 bytes of zeroes for
    * robustness2 semantics on read.
    */
   offs = align(offs, 16);
   dev->rodata.zero_sink = dev->rodata.bo->va->addr + offs;
   memset(dev->rodata.bo->map + offs, 0, 16);
   offs += 16;

   /* For null storage descriptors, we need to reserve 16 bytes to catch writes.
    * No particular content is required; we cannot get robustness2 semantics
    * without more work.
    */
   offs = align(offs, 16);
   dev->rodata.null_sink = dev->rodata.bo->va->addr + offs;
   offs += 16;

   return VK_SUCCESS;
}

static uint32_t
internal_key_hash(const void *key_)
{
   const struct hk_internal_key *key = key_;

   return _mesa_hash_data(key, sizeof(struct hk_internal_key) + key->key_size);
}

static bool
internal_key_equal(const void *a_, const void *b_)
{
   const struct hk_internal_key *a = a_;
   const struct hk_internal_key *b = b_;

   return a->builder == b->builder && a->key_size == b->key_size &&
          memcmp(a->key, b->key, a->key_size) == 0;
}

static VkResult
hk_init_internal_shaders(struct hk_internal_shaders *s)
{
   s->ht = _mesa_hash_table_create(NULL, internal_key_hash, internal_key_equal);
   if (!s->ht)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   simple_mtx_init(&s->lock, mtx_plain);
   return VK_SUCCESS;
}

static void
hk_destroy_internal_shaders(struct hk_device *dev,
                            struct hk_internal_shaders *s, bool part)
{
   hash_table_foreach(s->ht, ent) {
      if (part) {
         struct agx_shader_part *part = ent->data;
         free(part->binary);

         /* The agx_shader_part itself is ralloc'd against the hash table so
          * will be freed.
          */
      } else {
         struct hk_api_shader *obj = ent->data;
         hk_api_shader_destroy(&dev->vk, &obj->vk, NULL);
      }
   }

   _mesa_hash_table_destroy(s->ht, NULL);
   simple_mtx_destroy(&s->lock);
}

DERIVE_HASH_TABLE(agx_sampler_packed);

static VkResult
hk_init_sampler_heap(struct hk_device *dev, struct hk_sampler_heap *h)
{
   h->ht = agx_sampler_packed_table_create(NULL);
   if (!h->ht)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   VkResult result =
      hk_descriptor_table_init(dev, &h->table, AGX_SAMPLER_LENGTH, 1024, 1024);

   if (result != VK_SUCCESS) {
      ralloc_free(h->ht);
      return result;
   }

   simple_mtx_init(&h->lock, mtx_plain);
   return VK_SUCCESS;
}

static void
hk_destroy_sampler_heap(struct hk_device *dev, struct hk_sampler_heap *h)
{
   hk_descriptor_table_finish(dev, &h->table);
   ralloc_free(h->ht);
   simple_mtx_destroy(&h->lock);
}

static VkResult
hk_sampler_heap_add_locked(struct hk_device *dev, struct hk_sampler_heap *h,
                           struct agx_sampler_packed desc,
                           struct hk_rc_sampler **out)
{
   struct hash_entry *ent = _mesa_hash_table_search(h->ht, &desc);
   if (ent != NULL) {
      *out = ent->data;

      assert((*out)->refcount != 0);
      (*out)->refcount++;

      return VK_SUCCESS;
   }

   struct hk_rc_sampler *rc = ralloc(h->ht, struct hk_rc_sampler);
   if (!rc)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   uint32_t index;
   VkResult result =
      hk_descriptor_table_add(dev, &h->table, &desc, sizeof(desc), &index);
   if (result != VK_SUCCESS) {
      ralloc_free(rc);
      return result;
   }

   *rc = (struct hk_rc_sampler){
      .key = desc,
      .refcount = 1,
      .index = index,
   };

   _mesa_hash_table_insert(h->ht, &rc->key, rc);
   *out = rc;

   return VK_SUCCESS;
}

VkResult
hk_sampler_heap_add(struct hk_device *dev, struct agx_sampler_packed desc,
                    struct hk_rc_sampler **out)
{
   struct hk_sampler_heap *h = &dev->samplers;

   simple_mtx_lock(&h->lock);
   VkResult result = hk_sampler_heap_add_locked(dev, h, desc, out);
   simple_mtx_unlock(&h->lock);

   return result;
}

static void
hk_sampler_heap_remove_locked(struct hk_device *dev, struct hk_sampler_heap *h,
                              struct hk_rc_sampler *rc)
{
   assert(rc->refcount != 0);
   rc->refcount--;

   if (rc->refcount == 0) {
      hk_descriptor_table_remove(dev, &h->table, rc->index);
      _mesa_hash_table_remove_key(h->ht, &rc->key);
      ralloc_free(rc);
   }
}

void
hk_sampler_heap_remove(struct hk_device *dev, struct hk_rc_sampler *rc)
{
   struct hk_sampler_heap *h = &dev->samplers;

   simple_mtx_lock(&h->lock);
   hk_sampler_heap_remove_locked(dev, h, rc);
   simple_mtx_unlock(&h->lock);
}

/*
 * To implement nullDescriptor, the descriptor set code will reference
 * preuploaded null descriptors at fixed offsets in the image heap. Here we
 * upload those descriptors, initializing the image heap.
 */
static void
hk_upload_null_descriptors(struct hk_device *dev)
{
   struct agx_texture_packed null_tex;
   struct agx_pbe_packed null_pbe;
   uint32_t offset_tex, offset_pbe;

   agx_set_null_texture(&null_tex, dev->rodata.null_sink);
   agx_set_null_pbe(&null_pbe, dev->rodata.null_sink);

   hk_descriptor_table_add(dev, &dev->images, &null_tex, sizeof(null_tex),
                           &offset_tex);

   hk_descriptor_table_add(dev, &dev->images, &null_pbe, sizeof(null_pbe),
                           &offset_pbe);

   assert((offset_tex * HK_IMAGE_STRIDE) == HK_NULL_TEX_OFFSET && "static");
   assert((offset_pbe * HK_IMAGE_STRIDE) == HK_NULL_PBE_OFFSET && "static");
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateDevice(VkPhysicalDevice physicalDevice,
                const VkDeviceCreateInfo *pCreateInfo,
                const VkAllocationCallbacks *pAllocator, VkDevice *pDevice)
{
   VK_FROM_HANDLE(hk_physical_device, pdev, physicalDevice);
   VkResult result = VK_ERROR_OUT_OF_HOST_MEMORY;
   struct hk_device *dev;
   struct hk_instance *instance = (struct hk_instance *)pdev->vk.instance;

   dev = vk_zalloc2(&instance->vk.alloc, pAllocator, sizeof(*dev), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!dev)
      return vk_error(pdev, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_device_dispatch_table dispatch_table;

   /* For secondary command buffer support, overwrite any command entrypoints
    * in the main device-level dispatch table with
    * vk_cmd_enqueue_unless_primary_Cmd*.
    */
   vk_device_dispatch_table_from_entrypoints(
      &dispatch_table, &vk_cmd_enqueue_unless_primary_device_entrypoints, true);

   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &hk_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints, false);

   /* Populate primary cmd_dispatch table */
   vk_device_dispatch_table_from_entrypoints(&dev->cmd_dispatch,
                                             &hk_device_entrypoints, true);
   vk_device_dispatch_table_from_entrypoints(&dev->cmd_dispatch,
                                             &wsi_device_entrypoints, false);
   vk_device_dispatch_table_from_entrypoints(
      &dev->cmd_dispatch, &vk_common_device_entrypoints, false);

   result = vk_device_init(&dev->vk, &pdev->vk, &dispatch_table, pCreateInfo,
                           pAllocator);
   if (result != VK_SUCCESS)
      goto fail_alloc;

   dev->vk.shader_ops = &hk_device_shader_ops;
   dev->vk.command_dispatch_table = &dev->cmd_dispatch;

   drmDevicePtr drm_device = NULL;
   int ret = drmGetDeviceFromDevId(pdev->render_dev, 0, &drm_device);
   if (ret != 0) {
      result = vk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to get DRM device: %m");
      goto fail_init;
   }

   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   dev->dev.fd = open(path, O_RDWR | O_CLOEXEC);
   if (dev->dev.fd < 0) {
      drmFreeDevice(&drm_device);
      result = vk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                         "failed to open device %s", path);
      goto fail_init;
   }

   dev->perftest = debug_get_flags_option("HK_PERFTEST", hk_perf_options, 0);

   if (instance->no_border) {
      dev->perftest |= HK_PERF_NOBORDER;
   }

   if (HK_PERF(dev, NOROBUST)) {
      dev->vk.enabled_features.robustBufferAccess = false;
      dev->vk.enabled_features.robustBufferAccess2 = false;
      dev->vk.enabled_features.robustImageAccess = false;
      dev->vk.enabled_features.robustImageAccess2 = false;
      dev->vk.enabled_features.pipelineRobustness = false;
   }

   bool succ = agx_open_device(NULL, &dev->dev);
   drmFreeDevice(&drm_device);
   if (!succ) {
      result = vk_errorf(dev, VK_ERROR_INITIALIZATION_FAILED,
                         "Failed to get DRM device: %m");
      goto fail_fd;
   }

   vk_device_set_drm_fd(&dev->vk, dev->dev.fd);
   dev->vk.command_buffer_ops = &hk_cmd_buffer_ops;

   result = hk_descriptor_table_init(dev, &dev->images, AGX_TEXTURE_LENGTH,
                                     1024, 1024 * 1024);
   if (result != VK_SUCCESS)
      goto fail_dev;

   result = hk_init_sampler_heap(dev, &dev->samplers);
   if (result != VK_SUCCESS)
      goto fail_images;

   result = hk_descriptor_table_init(
      dev, &dev->occlusion_queries, sizeof(uint64_t), AGX_MAX_OCCLUSION_QUERIES,
      AGX_MAX_OCCLUSION_QUERIES);
   if (result != VK_SUCCESS)
      goto fail_samplers;

   result = hk_upload_rodata(dev);
   if (result != VK_SUCCESS)
      goto fail_queries;

   /* Depends on rodata */
   hk_upload_null_descriptors(dev);

   /* XXX: error handling, and should this even go on the device? */
   agx_bg_eot_init(&dev->bg_eot, &dev->dev);
   if (!dev->bg_eot.ht) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_rodata;
   }

   result = hk_init_internal_shaders(&dev->prolog_epilog);
   if (result != VK_SUCCESS)
      goto fail_bg_eot;

   result = hk_init_internal_shaders(&dev->kernels);
   if (result != VK_SUCCESS)
      goto fail_internal_shaders;

   result =
      hk_queue_init(dev, &dev->queue, &pCreateInfo->pQueueCreateInfos[0], 0);
   if (result != VK_SUCCESS)
      goto fail_internal_shaders_2;

   struct vk_pipeline_cache_create_info cache_info = {
      .weak_ref = true,
   };
   dev->mem_cache = vk_pipeline_cache_create(&dev->vk, &cache_info, NULL);
   if (dev->mem_cache == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto fail_queue;
   }

   result = hk_device_init_meta(dev);
   if (result != VK_SUCCESS)
      goto fail_mem_cache;

   *pDevice = hk_device_to_handle(dev);

   simple_mtx_init(&dev->scratch.lock, mtx_plain);
   agx_scratch_init(&dev->dev, &dev->scratch.vs);
   agx_scratch_init(&dev->dev, &dev->scratch.fs);
   agx_scratch_init(&dev->dev, &dev->scratch.cs);

   return VK_SUCCESS;

fail_mem_cache:
   vk_pipeline_cache_destroy(dev->mem_cache, NULL);
fail_queue:
   hk_queue_finish(dev, &dev->queue);
fail_rodata:
   agx_bo_unreference(&dev->dev, dev->rodata.bo);
fail_bg_eot:
   agx_bg_eot_cleanup(&dev->bg_eot);
fail_internal_shaders_2:
   hk_destroy_internal_shaders(dev, &dev->kernels, false);
fail_internal_shaders:
   hk_destroy_internal_shaders(dev, &dev->prolog_epilog, true);
fail_queries:
   hk_descriptor_table_finish(dev, &dev->occlusion_queries);
fail_samplers:
   hk_destroy_sampler_heap(dev, &dev->samplers);
fail_images:
   hk_descriptor_table_finish(dev, &dev->images);
fail_dev:
   agx_close_device(&dev->dev);
fail_fd:
   close(dev->dev.fd);
fail_init:
   vk_device_finish(&dev->vk);
fail_alloc:
   vk_free(&dev->vk.alloc, dev);
   return result;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyDevice(VkDevice _device, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, dev, _device);

   if (!dev)
      return;

   hk_device_finish_meta(dev);
   hk_destroy_internal_shaders(dev, &dev->kernels, false);
   hk_destroy_internal_shaders(dev, &dev->prolog_epilog, true);

   vk_pipeline_cache_destroy(dev->mem_cache, NULL);
   hk_queue_finish(dev, &dev->queue);
   vk_device_finish(&dev->vk);

   agx_scratch_fini(&dev->scratch.vs);
   agx_scratch_fini(&dev->scratch.fs);
   agx_scratch_fini(&dev->scratch.cs);
   simple_mtx_destroy(&dev->scratch.lock);

   hk_destroy_sampler_heap(dev, &dev->samplers);
   hk_descriptor_table_finish(dev, &dev->images);
   hk_descriptor_table_finish(dev, &dev->occlusion_queries);
   agx_bo_unreference(&dev->dev, dev->rodata.bo);
   agx_bo_unreference(&dev->dev, dev->heap);
   agx_bg_eot_cleanup(&dev->bg_eot);
   agx_close_device(&dev->dev);
   vk_free(&dev->vk.alloc, dev);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_GetCalibratedTimestampsKHR(
   VkDevice _device, uint32_t timestampCount,
   const VkCalibratedTimestampInfoKHR *pTimestampInfos, uint64_t *pTimestamps,
   uint64_t *pMaxDeviation)
{
   // VK_FROM_HANDLE(hk_device, dev, _device);
   // struct hk_physical_device *pdev = hk_device_physical(dev);
   uint64_t max_clock_period = 0;
   uint64_t begin, end;
   int d;

#ifdef CLOCK_MONOTONIC_RAW
   begin = vk_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   begin = vk_clock_gettime(CLOCK_MONOTONIC);
#endif

   for (d = 0; d < timestampCount; d++) {
      switch (pTimestampInfos[d].timeDomain) {
      case VK_TIME_DOMAIN_DEVICE_KHR:
         unreachable("todo");
         // pTimestamps[d] = agx_get_gpu_timestamp(&pdev->dev);
         max_clock_period = MAX2(
            max_clock_period, 1); /* FIXME: Is timestamp period actually 1? */
         break;
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR:
         pTimestamps[d] = vk_clock_gettime(CLOCK_MONOTONIC);
         max_clock_period = MAX2(max_clock_period, 1);
         break;

#ifdef CLOCK_MONOTONIC_RAW
      case VK_TIME_DOMAIN_CLOCK_MONOTONIC_RAW_KHR:
         pTimestamps[d] = begin;
         break;
#endif
      default:
         pTimestamps[d] = 0;
         break;
      }
   }

#ifdef CLOCK_MONOTONIC_RAW
   end = vk_clock_gettime(CLOCK_MONOTONIC_RAW);
#else
   end = vk_clock_gettime(CLOCK_MONOTONIC);
#endif

   *pMaxDeviation = vk_time_max_deviation(begin, end, max_clock_period);

   return VK_SUCCESS;
}
