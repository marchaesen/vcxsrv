/*
 * Copyright © 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RADV_DGC_H
#define RADV_DGC_H

#include "compiler/shader_enums.h"

#include "radv_constants.h"

#include "vk_device_generated_commands.h"

struct radv_cmd_buffer;
struct radv_device;
enum radv_queue_family;

struct radv_indirect_command_layout {
   struct vk_indirect_command_layout vk;

   uint64_t push_constant_mask;
   uint32_t push_constant_offsets[MAX_PUSH_CONSTANTS_SIZE / 4];
   uint64_t sequence_index_mask;

   VkPipelineLayout pipeline_layout;
   VkPipeline pipeline;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_indirect_command_layout, vk.base, VkIndirectCommandsLayoutEXT,
                               VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT)

struct radv_indirect_execution_set {
   struct vk_object_base base;

   struct radeon_winsys_bo *bo;
   uint64_t va;
   uint8_t *mapped_ptr;

   uint32_t stride;

   uint32_t compute_scratch_size_per_wave;
   uint32_t compute_scratch_waves;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(radv_indirect_execution_set, base, VkIndirectExecutionSetEXT,
                               VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);

uint32_t radv_dgc_get_buffer_alignment(const struct radv_device *device);

uint32_t radv_get_indirect_main_cmdbuf_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);
uint32_t radv_get_indirect_ace_cmdbuf_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);

uint32_t radv_get_indirect_main_cmdbuf_size(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);
uint32_t radv_get_indirect_ace_cmdbuf_size(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);

uint32_t radv_get_indirect_main_trailer_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);
uint32_t radv_get_indirect_ace_trailer_offset(const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);

void radv_prepare_dgc(struct radv_cmd_buffer *cmd_buffer, const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo,
                      struct radv_cmd_buffer *state_cmd_buffer, bool cond_render_enabled);

bool radv_use_dgc_predication(struct radv_cmd_buffer *cmd_buffer,
                              const VkGeneratedCommandsInfoEXT *pGeneratedCommandsInfo);

struct radv_shader *radv_dgc_get_shader(const VkGeneratedCommandsPipelineInfoEXT *pipeline_info,
                                        const VkGeneratedCommandsShaderInfoEXT *eso_info, gl_shader_stage stage);

#endif /* RADV_DGC_H */
