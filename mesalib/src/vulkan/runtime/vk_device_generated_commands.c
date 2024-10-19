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

#include "vk_alloc.h"
#include "vk_device.h"
#include "vk_device_generated_commands.h"
#include "vk_log.h"
#include "vk_util.h"

#include "util/compiler.h"

void *
vk_indirect_command_layout_create(struct vk_device *device,
                                  const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo,
                                  const VkAllocationCallbacks*                 pAllocator,
                                  size_t struct_size)
{
   struct vk_indirect_command_layout *elayout;
   uint32_t n_pc_layouts = 0, n_vb_layouts = 0;

   for (unsigned i = 0; i < pCreateInfo->tokenCount; i++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &pCreateInfo->pTokens[i];
      switch (token->type) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT:
         n_pc_layouts++;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
         n_vb_layouts++;
         break;
      default:
         break;
      }
   }

   VK_MULTIALLOC(ma);
   vk_multialloc_add_size_align(&ma, (void **)&elayout, struct_size, 8);
   VK_MULTIALLOC_DECL(&ma, struct vk_indirect_command_push_constant_layout, pc_layouts, n_pc_layouts);
   VK_MULTIALLOC_DECL(&ma, struct vk_indirect_command_vertex_layout, vb_layouts, n_vb_layouts);
   elayout = vk_multialloc_zalloc2(&ma, &device->alloc, pAllocator,
                                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!elayout)
      return NULL;

   vk_object_base_init(device, &elayout->base, VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_EXT);

   elayout->pc_layouts = pc_layouts;
   elayout->vb_layouts = vb_layouts;

   for (unsigned i = 0; i < pCreateInfo->tokenCount; i++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &pCreateInfo->pTokens[i];
      switch (token->type) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT:
         elayout->is_shaders = token->data.pExecutionSet->type == VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT;
         elayout->ies_src_offset_B = token->offset;
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_IES);
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
         assert(token->data.pVertexBuffer->vertexBindingUnit < 32);
         elayout->vertex_bindings |= BITFIELD_BIT(token->data.pVertexBuffer->vertexBindingUnit);
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_VB);
         vb_layouts[elayout->n_vb_layouts++] = (struct vk_indirect_command_vertex_layout) {
            .binding      = token->data.pVertexBuffer->vertexBindingUnit,
            .src_offset_B = token->offset,
         };
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT:
         elayout->index_mode_is_dx = token->data.pIndexBuffer->mode == VK_INDIRECT_COMMANDS_INPUT_MODE_DXGI_INDEX_BUFFER_EXT;
         elayout->index_src_offset_B = token->offset;
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_IB);
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT:
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_PC);
         pc_layouts[elayout->n_pc_layouts++] = (struct vk_indirect_command_push_constant_layout) {
            .stages       = token->data.pPushConstant->updateRange.stageFlags,
            .dst_offset_B = token->data.pPushConstant->updateRange.offset,
            .src_offset_B = token->offset,
            .size_B       = token->data.pPushConstant->updateRange.size,
         };
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT:
         assert(token->data.pPushConstant->updateRange.size == 4);
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_SI);
         elayout->si_layout = (struct vk_indirect_command_push_constant_layout) {
            .stages       = token->data.pPushConstant->updateRange.stageFlags,
            .dst_offset_B = token->data.pPushConstant->updateRange.offset,
            .src_offset_B = token->offset,
            .size_B       = token->data.pPushConstant->updateRange.size,
         };
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT:
         elayout->draw_count = true;
         FALLTHROUGH;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT:
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_DRAW);
         elayout->draw_src_offset_B = token->offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_COUNT_EXT:
         elayout->draw_count = true;
         FALLTHROUGH;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT:
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_DRAW_INDEXED);
         elayout->draw_src_offset_B = token->offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_NV_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_EXT:
         elayout->draw_count = true;
         FALLTHROUGH;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT:
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_DRAW_MESH);
         elayout->draw_src_offset_B = token->offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT:
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_DISPATCH);
         elayout->dispatch_src_offset_B = token->offset;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT:
         elayout->dgc_info |= BITFIELD_BIT(MESA_VK_DGC_RT);
         elayout->dispatch_src_offset_B = token->offset;
         break;
      default: break;
      }
   }

   if (elayout->dgc_info & (BITFIELD_BIT(MESA_VK_DGC_PC) | BITFIELD_BIT(MESA_VK_DGC_SI))) {
      if (pCreateInfo->pipelineLayout) {
         elayout->layout = pCreateInfo->pipelineLayout;
      } else {
         const struct vk_device_dispatch_table *disp = &device->dispatch_table;
         assert(device->enabled_features.dynamicGeneratedPipelineLayout);
         const VkPipelineLayoutCreateInfo *plci = vk_find_struct_const(pCreateInfo->pNext, PIPELINE_LAYOUT_CREATE_INFO);
         assert(plci);
         disp->CreatePipelineLayout(vk_device_to_handle(device), plci, NULL, &elayout->layout);
         elayout->delete_layout = true;
      }
   }
   elayout->stages = pCreateInfo->shaderStages;
   elayout->usage = pCreateInfo->flags;
   elayout->stride = pCreateInfo->indirectStride;
   elayout->token_count = pCreateInfo->tokenCount;

   return elayout;
}

void
vk_indirect_command_layout_destroy(struct vk_device *device,
                                   const VkAllocationCallbacks *pAllocator,
                                   struct vk_indirect_command_layout *elayout)
{
   if (elayout->delete_layout) {
      const struct vk_device_dispatch_table *disp = &device->dispatch_table;
      assert(device->enabled_features.dynamicGeneratedPipelineLayout);
      disp->DestroyPipelineLayout(vk_device_to_handle(device), elayout->layout, NULL);
   }
   vk_object_base_finish(&elayout->base);
   vk_free2(&device->alloc, pAllocator, elayout);
}
