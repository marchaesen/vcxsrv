/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_PIPELINE_H
#define RADV_PIPELINE_H

#include "util/mesa-sha1.h"

#include "vk_pipeline.h"
#include "vk_pipeline_cache.h"

#include "radv_radeon_winsys.h"

struct radv_device;
struct radv_shader_stage_key;
struct radv_shader_stage;
struct radv_pipeline_layout;
struct radv_graphics_state_key;
struct radv_shader_layout;
struct nir_shader;
typedef struct nir_shader nir_shader;

enum radv_pipeline_type {
   RADV_PIPELINE_GRAPHICS,
   RADV_PIPELINE_GRAPHICS_LIB,
   /* Compute pipeline */
   RADV_PIPELINE_COMPUTE,
   /* Raytracing pipeline */
   RADV_PIPELINE_RAY_TRACING,
   RADV_PIPELINE_TYPE_COUNT,
};

struct radv_pipeline {
   struct vk_object_base base;
   uint8_t sha1[SHA1_DIGEST_LENGTH];
   enum radv_pipeline_type type;

   VkPipelineCreateFlags2 create_flags;

   struct vk_pipeline_cache_object *cache_object;

   bool is_internal;
   bool need_indirect_descriptor_sets;
   struct radv_shader *shaders[MESA_VULKAN_SHADER_STAGES];
   struct radv_shader *gs_copy_shader;

   uint32_t user_data_0[MESA_VULKAN_SHADER_STAGES];

   /* Unique pipeline hash identifier. */
   uint64_t pipeline_hash;

   /* Pipeline layout info. */
   uint32_t push_constant_size;
   uint32_t dynamic_offset_count;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_pipeline, base, VkPipeline, VK_OBJECT_TYPE_PIPELINE)

#define RADV_DECL_PIPELINE_DOWNCAST(pipe_type, pipe_enum)                                                              \
   static inline struct radv_##pipe_type##_pipeline *radv_pipeline_to_##pipe_type(struct radv_pipeline *pipeline)      \
   {                                                                                                                   \
      assert(pipeline->type == pipe_enum);                                                                             \
      return (struct radv_##pipe_type##_pipeline *)pipeline;                                                           \
   }

bool radv_pipeline_capture_shaders(const struct radv_device *device, VkPipelineCreateFlags2 flags);

bool radv_shader_need_indirect_descriptor_sets(const struct radv_shader *shader);

bool radv_pipeline_capture_shader_stats(const struct radv_device *device, VkPipelineCreateFlags2 flags);

bool radv_pipeline_skip_shaders_cache(const struct radv_device *device, const struct radv_pipeline *pipeline);

void radv_pipeline_init(struct radv_device *device, struct radv_pipeline *pipeline, enum radv_pipeline_type type);

void radv_pipeline_destroy(struct radv_device *device, struct radv_pipeline *pipeline,
                           const VkAllocationCallbacks *allocator);

struct radv_shader_stage_key radv_pipeline_get_shader_key(const struct radv_device *device,
                                                          const VkPipelineShaderStageCreateInfo *stage,
                                                          VkPipelineCreateFlags2 flags, const void *pNext);

void radv_pipeline_stage_init(VkPipelineCreateFlags2 pipeline_flags, const VkPipelineShaderStageCreateInfo *sinfo,
                              const struct radv_pipeline_layout *layout, const struct radv_shader_stage_key *stage_key,
                              struct radv_shader_stage *out_stage);

void radv_shader_layout_init(const struct radv_pipeline_layout *pipeline_layout, gl_shader_stage stage,
                             struct radv_shader_layout *layout);

void radv_postprocess_nir(struct radv_device *device, const struct radv_graphics_state_key *gfx_state,
                          struct radv_shader_stage *stage);

bool radv_shader_should_clear_lds(const struct radv_device *device, const nir_shader *shader);

VkPipelineShaderStageCreateInfo *radv_copy_shader_stage_create_info(struct radv_device *device, uint32_t stageCount,
                                                                    const VkPipelineShaderStageCreateInfo *pStages,
                                                                    void *mem_ctx);

void radv_pipeline_hash(const struct radv_device *device, const struct radv_pipeline_layout *pipeline_layout,
                        struct mesa_sha1 *ctx);

void radv_pipeline_hash_shader_stage(VkPipelineCreateFlags2 pipeline_flags,
                                     const VkPipelineShaderStageCreateInfo *sinfo,
                                     const struct radv_shader_stage_key *stage_key, struct mesa_sha1 *ctx);

#endif /* RADV_PIPELINE_H */
