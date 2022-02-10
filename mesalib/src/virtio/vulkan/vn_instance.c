/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_instance.h"

#include "util/driconf.h"
#include "venus-protocol/vn_protocol_driver_info.h"
#include "venus-protocol/vn_protocol_driver_instance.h"
#include "venus-protocol/vn_protocol_driver_transport.h"

#include "vn_icd.h"
#include "vn_physical_device.h"
#include "vn_renderer.h"

#define VN_INSTANCE_LARGE_RING_SIZE (64 * 1024)
#define VN_INSTANCE_LARGE_RING_DIRECT_THRESHOLD                              \
   (VN_INSTANCE_LARGE_RING_SIZE / 16)

/* this must not exceed 2KiB for the ring to fit in a 4K page */
#define VN_INSTANCE_RING_SIZE (2 * 1024)
#define VN_INSTANCE_RING_DIRECT_THRESHOLD (VN_INSTANCE_RING_SIZE / 8)

/*
 * Instance extensions add instance-level or physical-device-level
 * functionalities.  It seems renderer support is either unnecessary or
 * optional.  We should be able to advertise them or lie about them locally.
 */
static const struct vk_instance_extension_table
   vn_instance_supported_extensions = {
      /* promoted to VK_VERSION_1_1 */
      .KHR_device_group_creation = true,
      .KHR_external_fence_capabilities = true,
      .KHR_external_memory_capabilities = true,
      .KHR_external_semaphore_capabilities = true,
      .KHR_get_physical_device_properties2 = true,

#ifdef VN_USE_WSI_PLATFORM
      .KHR_get_surface_capabilities2 = true,
      .KHR_surface = true,
      .KHR_surface_protected_capabilities = true,
#endif
#ifdef VK_USE_PLATFORM_WAYLAND_KHR
      .KHR_wayland_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XCB_KHR
      .KHR_xcb_surface = true,
#endif
#ifdef VK_USE_PLATFORM_XLIB_KHR
      .KHR_xlib_surface = true,
#endif
   };

static const driOptionDescription vn_dri_options[] = {
   /* clang-format off */
   DRI_CONF_SECTION_PERFORMANCE
      DRI_CONF_VK_X11_ENSURE_MIN_IMAGE_COUNT(false)
      DRI_CONF_VK_X11_OVERRIDE_MIN_IMAGE_COUNT(0)
      DRI_CONF_VK_X11_STRICT_IMAGE_COUNT(false)
   DRI_CONF_SECTION_END
   DRI_CONF_SECTION_DEBUG
      DRI_CONF_VK_WSI_FORCE_BGRA8_UNORM_FIRST(false)
   DRI_CONF_SECTION_END
   /* clang-format on */
};

static VkResult
vn_instance_init_renderer_versions(struct vn_instance *instance)
{
   uint32_t instance_version = 0;
   VkResult result =
      vn_call_vkEnumerateInstanceVersion(instance, &instance_version);
   if (result != VK_SUCCESS) {
      if (VN_DEBUG(INIT))
         vn_log(instance, "failed to enumerate renderer instance version");
      return result;
   }

   if (instance_version < VN_MIN_RENDERER_VERSION) {
      if (VN_DEBUG(INIT)) {
         vn_log(instance, "unsupported renderer instance version %d.%d",
                VK_VERSION_MAJOR(instance_version),
                VK_VERSION_MINOR(instance_version));
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   if (VN_DEBUG(INIT)) {
      vn_log(instance, "renderer instance version %d.%d.%d",
             VK_VERSION_MAJOR(instance_version),
             VK_VERSION_MINOR(instance_version),
             VK_VERSION_PATCH(instance_version));
   }

   /* request at least VN_MIN_RENDERER_VERSION internally */
   instance->renderer_api_version =
      MAX2(instance->base.base.app_info.api_version, VN_MIN_RENDERER_VERSION);

   /* instance version for internal use is capped */
   instance_version = MIN3(instance_version, instance->renderer_api_version,
                           instance->renderer_info.vk_xml_version);
   assert(instance_version >= VN_MIN_RENDERER_VERSION);

   instance->renderer_version = instance_version;

   return VK_SUCCESS;
}

static VkResult
vn_instance_init_ring(struct vn_instance *instance)
{
   const size_t buf_size = instance->experimental.largeRing
                              ? VN_INSTANCE_LARGE_RING_SIZE
                              : VN_INSTANCE_RING_SIZE;
   /* 32-bit seqno for renderer roundtrips */
   const size_t extra_size = sizeof(uint32_t);
   struct vn_ring_layout layout;
   vn_ring_get_layout(buf_size, extra_size, &layout);

   instance->ring.shmem =
      vn_renderer_shmem_create(instance->renderer, layout.shmem_size);
   if (!instance->ring.shmem) {
      if (VN_DEBUG(INIT))
         vn_log(instance, "failed to allocate/map ring shmem");
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   mtx_init(&instance->ring.mutex, mtx_plain);

   struct vn_ring *ring = &instance->ring.ring;
   vn_ring_init(ring, instance->renderer, &layout,
                instance->ring.shmem->mmap_ptr);

   instance->ring.id = (uintptr_t)ring;

   const struct VkRingCreateInfoMESA info = {
      .sType = VK_STRUCTURE_TYPE_RING_CREATE_INFO_MESA,
      .resourceId = instance->ring.shmem->res_id,
      .size = layout.shmem_size,
      .idleTimeout = 50ull * 1000 * 1000,
      .headOffset = layout.head_offset,
      .tailOffset = layout.tail_offset,
      .statusOffset = layout.status_offset,
      .bufferOffset = layout.buffer_offset,
      .bufferSize = layout.buffer_size,
      .extraOffset = layout.extra_offset,
      .extraSize = layout.extra_size,
   };

   uint32_t create_ring_data[64];
   struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
      create_ring_data, sizeof(create_ring_data));
   vn_encode_vkCreateRingMESA(&local_enc, 0, instance->ring.id, &info);
   vn_renderer_submit_simple(instance->renderer, create_ring_data,
                             vn_cs_encoder_get_len(&local_enc));

   vn_cs_encoder_init(&instance->ring.upload, instance,
                      VN_CS_ENCODER_STORAGE_SHMEM_ARRAY, 1 * 1024 * 1024);

   mtx_init(&instance->ring.roundtrip_mutex, mtx_plain);
   instance->ring.roundtrip_next = 1;

   return VK_SUCCESS;
}

static struct vn_renderer_shmem *
vn_instance_get_reply_shmem_locked(struct vn_instance *instance,
                                   size_t size,
                                   void **ptr);

static VkResult
vn_instance_init_experimental_features(struct vn_instance *instance)
{
   if (instance->renderer_info.vk_mesa_venus_protocol_spec_version !=
       100000) {
      if (VN_DEBUG(INIT))
         vn_log(instance, "renderer supports no experimental features");
      return VK_SUCCESS;
   }

   size_t struct_size = sizeof(instance->experimental);

   /* prepare the reply shmem */
   const size_t reply_size =
      vn_sizeof_vkGetVenusExperimentalFeatureData100000MESA_reply(
         &struct_size, &instance->experimental);
   void *reply_ptr;
   struct vn_renderer_shmem *reply_shmem =
      vn_instance_get_reply_shmem_locked(instance, reply_size, &reply_ptr);
   if (!reply_shmem)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* encode the command */
   uint32_t local_data[16];
   struct vn_cs_encoder local_enc =
      VN_CS_ENCODER_INITIALIZER_LOCAL(local_data, sizeof(local_data));
   vn_encode_vkGetVenusExperimentalFeatureData100000MESA(
      &local_enc, VK_COMMAND_GENERATE_REPLY_BIT_EXT, &struct_size,
      &instance->experimental);

   VkResult result = vn_renderer_submit_simple_sync(
      instance->renderer, local_data, vn_cs_encoder_get_len(&local_enc));
   if (result != VK_SUCCESS) {
      vn_renderer_shmem_unref(instance->renderer, reply_shmem);
      return result;
   }

   struct vn_cs_decoder reply_dec =
      VN_CS_DECODER_INITIALIZER(reply_ptr, reply_size);
   vn_decode_vkGetVenusExperimentalFeatureData100000MESA_reply(
      &reply_dec, &struct_size, &instance->experimental);
   vn_renderer_shmem_unref(instance->renderer, reply_shmem);

   if (VN_DEBUG(INIT)) {
      vn_log(instance,
             "VkVenusExperimentalFeatures100000MESA is as below:"
             "\n\tmemoryResourceAllocationSize = %u"
             "\n\tglobalFencing = %u"
             "\n\tlargeRing = %u",
             instance->experimental.memoryResourceAllocationSize,
             instance->experimental.globalFencing,
             instance->experimental.largeRing);
   }

   return VK_SUCCESS;
}

static VkResult
vn_instance_init_renderer(struct vn_instance *instance)
{
   const VkAllocationCallbacks *alloc = &instance->base.base.alloc;

   VkResult result = vn_renderer_create(instance, alloc, &instance->renderer);
   if (result != VK_SUCCESS)
      return result;

   vn_renderer_get_info(instance->renderer, &instance->renderer_info);

   uint32_t version = vn_info_wire_format_version();
   if (instance->renderer_info.wire_format_version != version) {
      if (VN_DEBUG(INIT)) {
         vn_log(instance, "wire format version %d != %d",
                instance->renderer_info.wire_format_version, version);
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   version = vn_info_vk_xml_version();
   if (instance->renderer_info.vk_xml_version > version)
      instance->renderer_info.vk_xml_version = version;
   if (instance->renderer_info.vk_xml_version < VN_MIN_RENDERER_VERSION) {
      if (VN_DEBUG(INIT)) {
         vn_log(instance, "vk xml version %d.%d.%d < %d.%d.%d",
                VK_VERSION_MAJOR(instance->renderer_info.vk_xml_version),
                VK_VERSION_MINOR(instance->renderer_info.vk_xml_version),
                VK_VERSION_PATCH(instance->renderer_info.vk_xml_version),
                VK_VERSION_MAJOR(VN_MIN_RENDERER_VERSION),
                VK_VERSION_MINOR(VN_MIN_RENDERER_VERSION),
                VK_VERSION_PATCH(VN_MIN_RENDERER_VERSION));
      }
      return VK_ERROR_INITIALIZATION_FAILED;
   }

   const struct vn_info_extension *ext =
      vn_info_extension_get("VK_EXT_command_serialization");
   if (instance->renderer_info.vk_ext_command_serialization_spec_version >
       ext->spec_version) {
      instance->renderer_info.vk_ext_command_serialization_spec_version =
         ext->spec_version;
   }

   ext = vn_info_extension_get("VK_MESA_venus_protocol");
   if (instance->renderer_info.vk_mesa_venus_protocol_spec_version >
       ext->spec_version) {
      instance->renderer_info.vk_mesa_venus_protocol_spec_version =
         ext->spec_version;
   }

   if (VN_DEBUG(INIT)) {
      vn_log(instance, "connected to renderer");
      vn_log(instance, "wire format version %d",
             instance->renderer_info.wire_format_version);
      vn_log(instance, "vk xml version %d.%d.%d",
             VK_VERSION_MAJOR(instance->renderer_info.vk_xml_version),
             VK_VERSION_MINOR(instance->renderer_info.vk_xml_version),
             VK_VERSION_PATCH(instance->renderer_info.vk_xml_version));
      vn_log(
         instance, "VK_EXT_command_serialization spec version %d",
         instance->renderer_info.vk_ext_command_serialization_spec_version);
      vn_log(instance, "VK_MESA_venus_protocol spec version %d",
             instance->renderer_info.vk_mesa_venus_protocol_spec_version);
      vn_log(instance, "supports blob id 0: %d",
             instance->renderer_info.supports_blob_id_0);
   }

   return VK_SUCCESS;
}

VkResult
vn_instance_submit_roundtrip(struct vn_instance *instance,
                             uint32_t *roundtrip_seqno)
{
   uint32_t write_ring_extra_data[8];
   struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
      write_ring_extra_data, sizeof(write_ring_extra_data));

   /* submit a vkWriteRingExtraMESA through the renderer */
   mtx_lock(&instance->ring.roundtrip_mutex);
   const uint32_t seqno = instance->ring.roundtrip_next++;
   vn_encode_vkWriteRingExtraMESA(&local_enc, 0, instance->ring.id, 0, seqno);
   VkResult result =
      vn_renderer_submit_simple(instance->renderer, write_ring_extra_data,
                                vn_cs_encoder_get_len(&local_enc));
   mtx_unlock(&instance->ring.roundtrip_mutex);

   *roundtrip_seqno = seqno;
   return result;
}

static bool
roundtrip_seqno_ge(uint32_t a, uint32_t b)
{
   /* a >= b, but deal with wrapping as well */
   return (a - b) <= INT32_MAX;
}

void
vn_instance_wait_roundtrip(struct vn_instance *instance,
                           uint32_t roundtrip_seqno)
{
   VN_TRACE_FUNC();
   const struct vn_ring *ring = &instance->ring.ring;
   const volatile atomic_uint *ptr = ring->shared.extra;
   uint32_t iter = 0;
   do {
      const uint32_t cur = atomic_load_explicit(ptr, memory_order_acquire);
      if (roundtrip_seqno_ge(cur, roundtrip_seqno))
         break;
      vn_relax(&iter, "roundtrip");
   } while (true);
}

struct vn_instance_submission {
   const struct vn_cs_encoder *cs;
   struct vn_ring_submit *submit;

   struct {
      struct vn_cs_encoder cs;
      struct vn_cs_encoder_buffer buffer;
      uint32_t data[64];
   } indirect;
};

static const struct vn_cs_encoder *
vn_instance_submission_get_cs(struct vn_instance_submission *submit,
                              const struct vn_cs_encoder *cs,
                              bool direct)
{
   if (direct)
      return cs;

   VkCommandStreamDescriptionMESA local_descs[8];
   VkCommandStreamDescriptionMESA *descs = local_descs;
   if (cs->buffer_count > ARRAY_SIZE(local_descs)) {
      descs =
         malloc(sizeof(VkCommandStreamDescriptionMESA) * cs->buffer_count);
      if (!descs)
         return NULL;
   }

   uint32_t desc_count = 0;
   for (uint32_t i = 0; i < cs->buffer_count; i++) {
      const struct vn_cs_encoder_buffer *buf = &cs->buffers[i];
      if (buf->committed_size) {
         descs[desc_count++] = (VkCommandStreamDescriptionMESA){
            .resourceId = buf->shmem->res_id,
            .offset = buf->offset,
            .size = buf->committed_size,
         };
      }
   }

   const size_t exec_size = vn_sizeof_vkExecuteCommandStreamsMESA(
      desc_count, descs, NULL, 0, NULL, 0);
   void *exec_data = submit->indirect.data;
   if (exec_size > sizeof(submit->indirect.data)) {
      exec_data = malloc(exec_size);
      if (!exec_data) {
         if (descs != local_descs)
            free(descs);
         return NULL;
      }
   }

   submit->indirect.buffer = VN_CS_ENCODER_BUFFER_INITIALIZER(exec_data);
   submit->indirect.cs =
      VN_CS_ENCODER_INITIALIZER(&submit->indirect.buffer, exec_size);
   vn_encode_vkExecuteCommandStreamsMESA(&submit->indirect.cs, 0, desc_count,
                                         descs, NULL, 0, NULL, 0);
   vn_cs_encoder_commit(&submit->indirect.cs);

   if (descs != local_descs)
      free(descs);

   return &submit->indirect.cs;
}

static struct vn_ring_submit *
vn_instance_submission_get_ring_submit(struct vn_ring *ring,
                                       const struct vn_cs_encoder *cs,
                                       struct vn_renderer_shmem *extra_shmem,
                                       bool direct)
{
   const uint32_t shmem_count =
      (direct ? 0 : cs->buffer_count) + (extra_shmem ? 1 : 0);
   struct vn_ring_submit *submit = vn_ring_get_submit(ring, shmem_count);
   if (!submit)
      return NULL;

   submit->shmem_count = shmem_count;
   if (!direct) {
      for (uint32_t i = 0; i < cs->buffer_count; i++) {
         submit->shmems[i] =
            vn_renderer_shmem_ref(ring->renderer, cs->buffers[i].shmem);
      }
   }
   if (extra_shmem) {
      submit->shmems[shmem_count - 1] =
         vn_renderer_shmem_ref(ring->renderer, extra_shmem);
   }

   return submit;
}

static void
vn_instance_submission_cleanup(struct vn_instance_submission *submit)
{
   if (submit->cs == &submit->indirect.cs &&
       submit->indirect.buffer.base != submit->indirect.data)
      free(submit->indirect.buffer.base);
}

static VkResult
vn_instance_submission_prepare(struct vn_instance_submission *submit,
                               const struct vn_cs_encoder *cs,
                               struct vn_ring *ring,
                               struct vn_renderer_shmem *extra_shmem,
                               bool direct)
{
   submit->cs = vn_instance_submission_get_cs(submit, cs, direct);
   if (!submit->cs)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   submit->submit =
      vn_instance_submission_get_ring_submit(ring, cs, extra_shmem, direct);
   if (!submit->submit) {
      vn_instance_submission_cleanup(submit);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

static bool
vn_instance_submission_can_direct(const struct vn_instance *instance,
                                  const struct vn_cs_encoder *cs)
{
   const size_t threshold = instance->experimental.largeRing
                               ? VN_INSTANCE_LARGE_RING_DIRECT_THRESHOLD
                               : VN_INSTANCE_RING_DIRECT_THRESHOLD;
   return vn_cs_encoder_get_len(cs) <= threshold;
}

static struct vn_cs_encoder *
vn_instance_ring_cs_upload_locked(struct vn_instance *instance,
                                  const struct vn_cs_encoder *cs)
{
   VN_TRACE_FUNC();
   assert(cs->storage_type == VN_CS_ENCODER_STORAGE_POINTER &&
          cs->buffer_count == 1);
   const void *cs_data = cs->buffers[0].base;
   const size_t cs_size = cs->total_committed_size;
   assert(cs_size == vn_cs_encoder_get_len(cs));

   struct vn_cs_encoder *upload = &instance->ring.upload;
   vn_cs_encoder_reset(upload);

   if (!vn_cs_encoder_reserve(upload, cs_size))
      return NULL;

   vn_cs_encoder_write(upload, cs_size, cs_data, cs_size);
   vn_cs_encoder_commit(upload);
   vn_instance_wait_roundtrip(instance, upload->current_buffer_roundtrip);

   return upload;
}

static VkResult
vn_instance_ring_submit_locked(struct vn_instance *instance,
                               const struct vn_cs_encoder *cs,
                               struct vn_renderer_shmem *extra_shmem,
                               uint32_t *ring_seqno)
{
   struct vn_ring *ring = &instance->ring.ring;

   const bool direct = vn_instance_submission_can_direct(instance, cs);
   if (!direct && cs->storage_type == VN_CS_ENCODER_STORAGE_POINTER) {
      cs = vn_instance_ring_cs_upload_locked(instance, cs);
      if (!cs)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      assert(cs->storage_type != VN_CS_ENCODER_STORAGE_POINTER);
   }

   struct vn_instance_submission submit;
   VkResult result =
      vn_instance_submission_prepare(&submit, cs, ring, extra_shmem, direct);
   if (result != VK_SUCCESS)
      return result;

   uint32_t seqno;
   const bool notify = vn_ring_submit(ring, submit.submit, submit.cs, &seqno);
   if (notify) {
      uint32_t notify_ring_data[8];
      struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
         notify_ring_data, sizeof(notify_ring_data));
      vn_encode_vkNotifyRingMESA(&local_enc, 0, instance->ring.id, seqno, 0);
      vn_renderer_submit_simple(instance->renderer, notify_ring_data,
                                vn_cs_encoder_get_len(&local_enc));
   }

   vn_instance_submission_cleanup(&submit);

   if (ring_seqno)
      *ring_seqno = seqno;

   return VK_SUCCESS;
}

VkResult
vn_instance_ring_submit(struct vn_instance *instance,
                        const struct vn_cs_encoder *cs)
{
   mtx_lock(&instance->ring.mutex);
   VkResult result = vn_instance_ring_submit_locked(instance, cs, NULL, NULL);
   mtx_unlock(&instance->ring.mutex);

   return result;
}

static struct vn_renderer_shmem *
vn_instance_get_reply_shmem_locked(struct vn_instance *instance,
                                   size_t size,
                                   void **out_ptr)
{
   VN_TRACE_FUNC();
   struct vn_renderer_shmem_pool *pool = &instance->reply_shmem_pool;
   const struct vn_renderer_shmem *saved_pool_shmem = pool->shmem;

   size_t offset;
   struct vn_renderer_shmem *shmem =
      vn_renderer_shmem_pool_alloc(instance->renderer, pool, size, &offset);
   if (!shmem)
      return NULL;

   assert(shmem == pool->shmem);
   *out_ptr = shmem->mmap_ptr + offset;

   if (shmem != saved_pool_shmem) {
      uint32_t set_reply_command_stream_data[16];
      struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
         set_reply_command_stream_data,
         sizeof(set_reply_command_stream_data));
      const struct VkCommandStreamDescriptionMESA stream = {
         .resourceId = shmem->res_id,
         .size = pool->size,
      };
      vn_encode_vkSetReplyCommandStreamMESA(&local_enc, 0, &stream);
      vn_cs_encoder_commit(&local_enc);

      /* vn_instance_init_experimental_features calls this before the ring is
       * created
       */
      if (likely(instance->ring.id)) {
         vn_instance_roundtrip(instance);
         vn_instance_ring_submit_locked(instance, &local_enc, NULL, NULL);
      } else {
         vn_renderer_submit_simple(instance->renderer,
                                   set_reply_command_stream_data,
                                   vn_cs_encoder_get_len(&local_enc));
      }
   }

   /* TODO avoid this seek command and go lock-free? */
   uint32_t seek_reply_command_stream_data[8];
   struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
      seek_reply_command_stream_data, sizeof(seek_reply_command_stream_data));
   vn_encode_vkSeekReplyCommandStreamMESA(&local_enc, 0, offset);
   vn_cs_encoder_commit(&local_enc);

   /* vn_instance_init_experimental_features calls this before the ring is
    * created
    */
   if (likely(instance->ring.id)) {
      vn_instance_ring_submit_locked(instance, &local_enc, NULL, NULL);
   } else {
      vn_renderer_submit_simple(instance->renderer,
                                seek_reply_command_stream_data,
                                vn_cs_encoder_get_len(&local_enc));
   }

   return shmem;
}

void
vn_instance_submit_command(struct vn_instance *instance,
                           struct vn_instance_submit_command *submit)
{
   void *reply_ptr = NULL;
   submit->reply_shmem = NULL;

   mtx_lock(&instance->ring.mutex);

   if (vn_cs_encoder_is_empty(&submit->command))
      goto fail;
   vn_cs_encoder_commit(&submit->command);

   if (submit->reply_size) {
      submit->reply_shmem = vn_instance_get_reply_shmem_locked(
         instance, submit->reply_size, &reply_ptr);
      if (!submit->reply_shmem)
         goto fail;
   }

   uint32_t ring_seqno;
   VkResult result = vn_instance_ring_submit_locked(
      instance, &submit->command, submit->reply_shmem, &ring_seqno);

   mtx_unlock(&instance->ring.mutex);

   submit->reply = VN_CS_DECODER_INITIALIZER(reply_ptr, submit->reply_size);

   if (submit->reply_size && result == VK_SUCCESS)
      vn_ring_wait(&instance->ring.ring, ring_seqno);

   return;

fail:
   instance->ring.command_dropped++;
   mtx_unlock(&instance->ring.mutex);
}

/* instance commands */

VkResult
vn_EnumerateInstanceVersion(uint32_t *pApiVersion)
{
   *pApiVersion = VN_MAX_API_VERSION;
   return VK_SUCCESS;
}

VkResult
vn_EnumerateInstanceExtensionProperties(const char *pLayerName,
                                        uint32_t *pPropertyCount,
                                        VkExtensionProperties *pProperties)
{
   if (pLayerName)
      return vn_error(NULL, VK_ERROR_LAYER_NOT_PRESENT);

   return vk_enumerate_instance_extension_properties(
      &vn_instance_supported_extensions, pPropertyCount, pProperties);
}

VkResult
vn_EnumerateInstanceLayerProperties(uint32_t *pPropertyCount,
                                    VkLayerProperties *pProperties)
{
   *pPropertyCount = 0;
   return VK_SUCCESS;
}

VkResult
vn_CreateInstance(const VkInstanceCreateInfo *pCreateInfo,
                  const VkAllocationCallbacks *pAllocator,
                  VkInstance *pInstance)
{
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : vk_default_allocator();
   struct vn_instance *instance;
   VkResult result;

   vn_debug_init();
   vn_trace_init();

   instance = vk_zalloc(alloc, sizeof(*instance), VN_DEFAULT_ALIGN,
                        VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!instance)
      return vn_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   struct vk_instance_dispatch_table dispatch_table;
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &vn_instance_entrypoints, true);
   vk_instance_dispatch_table_from_entrypoints(
      &dispatch_table, &wsi_instance_entrypoints, false);
   result = vn_instance_base_init(&instance->base,
                                  &vn_instance_supported_extensions,
                                  &dispatch_table, pCreateInfo, alloc);
   if (result != VK_SUCCESS) {
      vk_free(alloc, instance);
      return vn_error(NULL, result);
   }

   mtx_init(&instance->physical_device.mutex, mtx_plain);
   mtx_init(&instance->cs_shmem.mutex, mtx_plain);

   if (!vn_icd_supports_api_version(
          instance->base.base.app_info.api_version)) {
      result = VK_ERROR_INCOMPATIBLE_DRIVER;
      goto fail;
   }

   if (pCreateInfo->enabledLayerCount) {
      result = VK_ERROR_LAYER_NOT_PRESENT;
      goto fail;
   }

   result = vn_instance_init_renderer(instance);
   if (result != VK_SUCCESS)
      goto fail;

   vn_renderer_shmem_pool_init(instance->renderer,
                               &instance->reply_shmem_pool, 1u << 20);

   result = vn_instance_init_experimental_features(instance);
   if (result != VK_SUCCESS)
      goto fail;

   result = vn_instance_init_ring(instance);
   if (result != VK_SUCCESS)
      goto fail;

   result = vn_instance_init_renderer_versions(instance);
   if (result != VK_SUCCESS)
      goto fail;

   vn_renderer_shmem_pool_init(instance->renderer, &instance->cs_shmem.pool,
                               8u << 20);

   VkInstanceCreateInfo local_create_info = *pCreateInfo;
   local_create_info.ppEnabledExtensionNames = NULL;
   local_create_info.enabledExtensionCount = 0;
   pCreateInfo = &local_create_info;

   VkApplicationInfo local_app_info;
   if (instance->base.base.app_info.api_version <
       instance->renderer_api_version) {
      if (pCreateInfo->pApplicationInfo) {
         local_app_info = *pCreateInfo->pApplicationInfo;
         local_app_info.apiVersion = instance->renderer_api_version;
      } else {
         local_app_info = (const VkApplicationInfo){
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .apiVersion = instance->renderer_api_version,
         };
      }
      local_create_info.pApplicationInfo = &local_app_info;
   }

   VkInstance instance_handle = vn_instance_to_handle(instance);
   result =
      vn_call_vkCreateInstance(instance, pCreateInfo, NULL, &instance_handle);
   if (result != VK_SUCCESS)
      goto fail;

   driParseOptionInfo(&instance->available_dri_options, vn_dri_options,
                      ARRAY_SIZE(vn_dri_options));
   driParseConfigFiles(&instance->dri_options,
                       &instance->available_dri_options, 0, "venus", NULL,
                       NULL, instance->base.base.app_info.app_name,
                       instance->base.base.app_info.app_version,
                       instance->base.base.app_info.engine_name,
                       instance->base.base.app_info.engine_version);

   *pInstance = instance_handle;

   return VK_SUCCESS;

fail:
   if (instance->ring.shmem) {
      uint32_t destroy_ring_data[4];
      struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
         destroy_ring_data, sizeof(destroy_ring_data));
      vn_encode_vkDestroyRingMESA(&local_enc, 0, instance->ring.id);
      vn_renderer_submit_simple(instance->renderer, destroy_ring_data,
                                vn_cs_encoder_get_len(&local_enc));

      mtx_destroy(&instance->ring.roundtrip_mutex);
      vn_cs_encoder_fini(&instance->ring.upload);
      vn_renderer_shmem_unref(instance->renderer, instance->ring.shmem);
      vn_ring_fini(&instance->ring.ring);
      mtx_destroy(&instance->ring.mutex);
   }

   vn_renderer_shmem_pool_fini(instance->renderer,
                               &instance->reply_shmem_pool);

   if (instance->renderer)
      vn_renderer_destroy(instance->renderer, alloc);

   mtx_destroy(&instance->physical_device.mutex);
   mtx_destroy(&instance->cs_shmem.mutex);

   vn_instance_base_fini(&instance->base);
   vk_free(alloc, instance);

   return vn_error(NULL, result);
}

void
vn_DestroyInstance(VkInstance _instance,
                   const VkAllocationCallbacks *pAllocator)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &instance->base.base.alloc;

   if (!instance)
      return;

   if (instance->physical_device.initialized) {
      for (uint32_t i = 0; i < instance->physical_device.device_count; i++)
         vn_physical_device_fini(&instance->physical_device.devices[i]);
      vk_free(alloc, instance->physical_device.devices);
      vk_free(alloc, instance->physical_device.groups);
   }
   mtx_destroy(&instance->physical_device.mutex);

   vn_call_vkDestroyInstance(instance, _instance, NULL);

   vn_renderer_shmem_pool_fini(instance->renderer, &instance->cs_shmem.pool);
   mtx_destroy(&instance->cs_shmem.mutex);

   uint32_t destroy_ring_data[4];
   struct vn_cs_encoder local_enc = VN_CS_ENCODER_INITIALIZER_LOCAL(
      destroy_ring_data, sizeof(destroy_ring_data));
   vn_encode_vkDestroyRingMESA(&local_enc, 0, instance->ring.id);
   vn_renderer_submit_simple(instance->renderer, destroy_ring_data,
                             vn_cs_encoder_get_len(&local_enc));

   mtx_destroy(&instance->ring.roundtrip_mutex);
   vn_cs_encoder_fini(&instance->ring.upload);
   vn_ring_fini(&instance->ring.ring);
   mtx_destroy(&instance->ring.mutex);
   vn_renderer_shmem_unref(instance->renderer, instance->ring.shmem);

   vn_renderer_shmem_pool_fini(instance->renderer,
                               &instance->reply_shmem_pool);

   vn_renderer_destroy(instance->renderer, alloc);

   driDestroyOptionCache(&instance->dri_options);
   driDestroyOptionInfo(&instance->available_dri_options);

   vn_instance_base_fini(&instance->base);
   vk_free(alloc, instance);
}

PFN_vkVoidFunction
vn_GetInstanceProcAddr(VkInstance _instance, const char *pName)
{
   struct vn_instance *instance = vn_instance_from_handle(_instance);
   return vk_instance_get_proc_addr(&instance->base.base,
                                    &vn_instance_entrypoints, pName);
}
