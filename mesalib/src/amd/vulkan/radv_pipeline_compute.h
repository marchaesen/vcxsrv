/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_PIPELINE_COMPUTE_H
#define RADV_PIPELINE_COMPUTE_H

#include "radv_pipeline.h"

struct radv_physical_device;
struct radv_shader_binary;
struct radv_shader_info;

struct radv_compute_pipeline {
   struct radv_pipeline base;
};

RADV_DECL_PIPELINE_DOWNCAST(compute, RADV_PIPELINE_COMPUTE)

struct radv_compute_pipeline_metadata {
   uint32_t wave32;
   uint32_t grid_base_sgpr;
   uint32_t push_const_sgpr;
   uint64_t inline_push_const_mask;
   uint32_t indirect_desc_sets_sgpr;
};

uint32_t radv_get_compute_resource_limits(const struct radv_physical_device *pdev, const struct radv_shader_info *info);

void radv_get_compute_shader_metadata(const struct radv_device *device, const struct radv_shader *cs,
                                      struct radv_compute_pipeline_metadata *metadata);

void radv_compute_pipeline_init(struct radv_compute_pipeline *pipeline, const struct radv_pipeline_layout *layout,
                                struct radv_shader *shader);

struct radv_shader *radv_compile_cs(struct radv_device *device, struct vk_pipeline_cache *cache,
                                    struct radv_shader_stage *cs_stage, bool keep_executable_info,
                                    bool keep_statistic_info, bool is_internal, bool skip_shaders_cache,
                                    struct radv_shader_binary **cs_binary);

VkResult radv_compute_pipeline_create(VkDevice _device, VkPipelineCache _cache,
                                      const VkComputePipelineCreateInfo *pCreateInfo,
                                      const VkAllocationCallbacks *pAllocator, VkPipeline *pPipeline);

void radv_destroy_compute_pipeline(struct radv_device *device, struct radv_compute_pipeline *pipeline);

void radv_compute_pipeline_hash(const struct radv_device *device, const VkComputePipelineCreateInfo *pCreateInfo,
                                unsigned char *hash);

#endif /* RADV_PIPELINE_COMPUTE_H */
