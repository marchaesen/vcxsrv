/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "asahi/lib/agx_device.h"
#include "util/simple_mtx.h"
#include "agx_bg_eot.h"
#include "agx_pack.h"
#include "agx_scratch.h"
#include "decode.h"
#include "vk_cmd_queue.h"
#include "vk_dispatch_table.h"

#include "hk_private.h"

#include "hk_descriptor_table.h"
#include "hk_queue.h"
#include "vk_device.h"
#include "vk_meta.h"
#include "vk_queue.h"

struct hk_physical_device;
struct vk_pipeline_cache;

/* Fixed offsets for reserved null image descriptors */
#define HK_NULL_TEX_OFFSET (0)
#define HK_NULL_PBE_OFFSET (24)

typedef void (*hk_internal_builder_t)(struct nir_builder *b, const void *key);

struct hk_internal_key {
   hk_internal_builder_t builder;
   size_t key_size;
   uint8_t key[];
};

struct hk_internal_shaders {
   simple_mtx_t lock;
   struct hash_table *ht;
};

struct hk_rc_sampler {
   struct agx_sampler_packed key;

   /* Reference count for this hardware sampler, protected by the heap mutex */
   uint16_t refcount;

   /* Index of this hardware sampler in the hardware sampler heap */
   uint16_t index;
};

struct hk_sampler_heap {
   simple_mtx_t lock;

   struct hk_descriptor_table table;

   /* Map of agx_sampler_packed to hk_rc_sampler */
   struct hash_table *ht;
};

struct hk_device {
   struct vk_device vk;
   struct agx_device dev;
   struct agxdecode_ctx *decode_ctx;

   struct hk_descriptor_table images;
   struct hk_descriptor_table occlusion_queries;
   struct hk_sampler_heap samplers;

   struct hk_queue queue;

   struct vk_pipeline_cache *mem_cache;

   struct vk_meta_device meta;
   struct agx_bg_eot_cache bg_eot;

   struct {
      struct agx_bo *bo;
      struct agx_usc_sampler_packed txf_sampler;
      struct agx_usc_uniform_packed image_heap;
      uint64_t null_sink, zero_sink;
      uint64_t geometry_state;
   } rodata;

   struct hk_internal_shaders prolog_epilog;
   struct hk_internal_shaders kernels;
   struct hk_api_shader *write_shader;

   /* Indirected for common secondary emulation */
   struct vk_device_dispatch_table cmd_dispatch;

   /* Heap used for GPU-side memory allocation for geometry/tessellation.
    *
    * Control streams accessing the heap must be serialized. This is not
    * expected to be a legitimate problem. If it is, we can rework later.
    */
   struct agx_bo *heap;

   struct {
      struct agx_scratch vs, fs, cs;
      simple_mtx_t lock;
   } scratch;

   uint32_t perftest;
};

VK_DEFINE_HANDLE_CASTS(hk_device, vk.base, VkDevice, VK_OBJECT_TYPE_DEVICE)

enum hk_perftest {
   HK_PERF_NOTESS = BITFIELD_BIT(0),
   HK_PERF_NOBORDER = BITFIELD_BIT(1),
   HK_PERF_NOBARRIER = BITFIELD_BIT(2),
   HK_PERF_BATCH = BITFIELD_BIT(3),
   HK_PERF_NOROBUST = BITFIELD_BIT(4),
};

#define HK_PERF(dev, flag) unlikely((dev)->perftest &HK_PERF_##flag)

static inline struct hk_physical_device *
hk_device_physical(struct hk_device *dev)
{
   return (struct hk_physical_device *)dev->vk.physical;
}

VkResult hk_device_init_meta(struct hk_device *dev);
void hk_device_finish_meta(struct hk_device *dev);

VkResult hk_sampler_heap_add(struct hk_device *dev,
                             struct agx_sampler_packed desc,
                             struct hk_rc_sampler **out);

void hk_sampler_heap_remove(struct hk_device *dev, struct hk_rc_sampler *rc);

static inline struct agx_scratch *
hk_device_scratch_locked(struct hk_device *dev, enum pipe_shader_type stage)
{
   simple_mtx_assert_locked(&dev->scratch.lock);

   switch (stage) {
   case PIPE_SHADER_FRAGMENT:
      return &dev->scratch.fs;
   case PIPE_SHADER_VERTEX:
      return &dev->scratch.vs;
   default:
      return &dev->scratch.cs;
   }
}

static inline void
hk_device_alloc_scratch(struct hk_device *dev, enum pipe_shader_type stage,
                        unsigned size)
{
   simple_mtx_lock(&dev->scratch.lock);
   agx_scratch_alloc(hk_device_scratch_locked(dev, stage), size, 0);
   simple_mtx_unlock(&dev->scratch.lock);
}
