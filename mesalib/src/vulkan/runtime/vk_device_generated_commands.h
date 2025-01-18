/*
 * Copyright © 2024 Valve Corporation
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

#ifndef VK_DEVICE_GENERATED_COMMANDS_H
#define VK_DEVICE_GENERATED_COMMANDS_H

#include <vulkan/vulkan_core.h>
#include "vk_object.h"

enum mesa_vk_dgc_types {
   MESA_VK_DGC_IES,
   MESA_VK_DGC_PC,
   MESA_VK_DGC_SI,
   MESA_VK_DGC_IB,
   MESA_VK_DGC_VB,
   MESA_VK_DGC_DRAW,
   MESA_VK_DGC_DRAW_INDEXED,
   MESA_VK_DGC_DRAW_MESH,
   MESA_VK_DGC_DISPATCH,
   MESA_VK_DGC_RT,
};

struct vk_indirect_command_vertex_layout {
   uint32_t binding;
   uint32_t src_offset_B;
};

struct vk_indirect_command_push_constant_layout {
   VkShaderStageFlags stages;
   uint32_t dst_offset_B;
   uint32_t src_offset_B;
   uint32_t size_B;
};

/* this struct must come first in the parent class,
 * the final member of the parent class must be
   VkIndirectCommandsLayoutTokenEXT tokens[0];
 */
struct vk_indirect_command_layout {
   struct vk_object_base base;

   /* mask of mesa_vk_dgc_types */
   uint32_t dgc_info;

   VkPipelineLayout layout;

   VkIndirectCommandsLayoutUsageFlagsEXT usage;
   VkShaderStageFlags stages;

   size_t stride;

   uint32_t vertex_bindings;

   uint32_t ies_src_offset_B;
   bool is_shaders;

   bool delete_layout;

   bool index_mode_is_dx;
   uint32_t index_src_offset_B;

   uint32_t draw_src_offset_B;
   bool draw_count;

   uint32_t dispatch_src_offset_B;

   unsigned token_count;

   struct vk_indirect_command_push_constant_layout si_layout;

   uint32_t n_pc_layouts;
   struct vk_indirect_command_push_constant_layout *pc_layouts;

   uint32_t n_vb_layouts;
   struct vk_indirect_command_vertex_layout *vb_layouts;
};

void *
vk_indirect_command_layout_create(struct vk_device *device,
                                  const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo,
                                  const VkAllocationCallbacks* pAllocator,
                                  size_t struct_size);

void
vk_indirect_command_layout_destroy(struct vk_device *device,
                                   const VkAllocationCallbacks *pAllocator,
                                   struct vk_indirect_command_layout *elayout);

#endif /* VK_DEVICE_GENERATED_COMMANDS_H */
