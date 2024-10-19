/*
 * Copyright © 2023 Valve Corporation
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

#include "lvp_private.h"

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateIndirectCommandsLayoutNV(
    VkDevice                                    _device,
    const VkIndirectCommandsLayoutCreateInfoNV* pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkIndirectCommandsLayoutNV*                 pIndirectCommandsLayout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_indirect_command_layout_nv *dlayout;

   size_t size = sizeof(*dlayout) + pCreateInfo->tokenCount * sizeof(VkIndirectCommandsLayoutTokenNV);

   dlayout =
      vk_zalloc2(&device->vk.alloc, pAllocator, size, alignof(struct lvp_indirect_command_layout_nv),
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!dlayout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &dlayout->base, VK_OBJECT_TYPE_INDIRECT_COMMANDS_LAYOUT_NV);

   dlayout->stream_count = pCreateInfo->streamCount;
   dlayout->token_count = pCreateInfo->tokenCount;
   for (unsigned i = 0; i < pCreateInfo->streamCount; i++)
      dlayout->stream_strides[i] = pCreateInfo->pStreamStrides[i];
   typed_memcpy(dlayout->tokens, pCreateInfo->pTokens, pCreateInfo->tokenCount);

   *pIndirectCommandsLayout = lvp_indirect_command_layout_nv_to_handle(dlayout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyIndirectCommandsLayoutNV(
    VkDevice                                    _device,
    VkIndirectCommandsLayoutNV                  indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   VK_FROM_HANDLE(lvp_indirect_command_layout_nv, layout, indirectCommandsLayout);

   if (!layout)
      return;

   vk_object_base_finish(&layout->base);
   vk_free2(&device->vk.alloc, pAllocator, layout);
}

enum vk_cmd_type
lvp_nv_dgc_token_to_cmd_type(const VkIndirectCommandsLayoutTokenNV *token)
{
   switch (token->tokenType) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV:
         return VK_CMD_BIND_PIPELINE_SHADER_GROUP_NV;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_STATE_FLAGS_NV:
         if (token->indirectStateFlags & VK_INDIRECT_STATE_FLAG_FRONTFACE_BIT_NV) {
            return VK_CMD_SET_FRONT_FACE;
         }
         assert(!"unknown token type!");
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV:
         return VK_CMD_PUSH_CONSTANTS2_KHR;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV:
         return VK_CMD_BIND_INDEX_BUFFER;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV:
        return VK_CMD_BIND_VERTEX_BUFFERS2;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV:
         return VK_CMD_DRAW_INDEXED_INDIRECT;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV:
         return VK_CMD_DRAW_INDIRECT;
      // only available if VK_EXT_mesh_shader is supported
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV:
         return VK_CMD_DRAW_MESH_TASKS_INDIRECT_EXT;
      // only available if VK_NV_mesh_shader is supported
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_TASKS_NV:
         unreachable("NV_mesh_shader unsupported!");
      default:
         unreachable("unknown token type");
   }
   return UINT32_MAX;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetGeneratedCommandsMemoryRequirementsNV(
    VkDevice                                    device,
    const VkGeneratedCommandsMemoryRequirementsInfoNV* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   VK_FROM_HANDLE(lvp_indirect_command_layout_nv, dlayout, pInfo->indirectCommandsLayout);

   size_t size = sizeof(struct list_head);

   for (unsigned i = 0; i < dlayout->token_count; i++) {
      const VkIndirectCommandsLayoutTokenNV *token = &dlayout->tokens[i];
      UNUSED struct vk_cmd_queue_entry *cmd;
      enum vk_cmd_type type = lvp_nv_dgc_token_to_cmd_type(token);
      size += vk_cmd_queue_type_sizes[type];

      switch (token->tokenType) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_NV:
         size += sizeof(*cmd->u.bind_vertex_buffers.buffers);
         size += sizeof(*cmd->u.bind_vertex_buffers.offsets);
         size += sizeof(*cmd->u.bind_vertex_buffers2.sizes) + sizeof(*cmd->u.bind_vertex_buffers2.strides);
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_NV:
         size += token->pushconstantSize + sizeof(VkPushConstantsInfoKHR);
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SHADER_GROUP_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_STATE_FLAGS_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_TASKS_NV:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV:
         break;
      default:
         unreachable("unknown type!");
      }
   }

   size *= pInfo->maxSequencesCount;

   pMemoryRequirements->memoryRequirements.memoryTypeBits = 1;
   pMemoryRequirements->memoryRequirements.alignment = 4;
   pMemoryRequirements->memoryRequirements.size = align(size, pMemoryRequirements->memoryRequirements.alignment);
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateIndirectExecutionSetEXT(
    VkDevice                                   _device,
    const VkIndirectExecutionSetCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks*               pAllocator,
    VkIndirectExecutionSetEXT*                 pIndirectExecutionSet)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   bool is_shaders = pCreateInfo->type == VK_INDIRECT_EXECUTION_SET_INFO_TYPE_SHADER_OBJECTS_EXT;
   size_t size = 0;
   if (is_shaders) {
      size += pCreateInfo->info.pShaderInfo->maxShaderCount;
   } else {
      size += pCreateInfo->info.pPipelineInfo->maxPipelineCount;
   }
   size *= sizeof(int64_t);
   size += sizeof(struct lvp_indirect_execution_set);

   struct lvp_indirect_execution_set *iset =
      vk_zalloc2(&device->vk.alloc, pAllocator, size, alignof(struct lvp_indirect_execution_set),
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!iset)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &iset->base, VK_OBJECT_TYPE_INDIRECT_EXECUTION_SET_EXT);
   iset->is_shaders = is_shaders;

   if (is_shaders) {
      for (unsigned i = 0; i < pCreateInfo->info.pShaderInfo->shaderCount; i++)
         iset->array[i] = pCreateInfo->info.pShaderInfo->pInitialShaders[i];
   } else {
      iset->array[0] = pCreateInfo->info.pPipelineInfo->initialPipeline;
   }

   *pIndirectExecutionSet = lvp_indirect_execution_set_to_handle(iset);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyIndirectExecutionSetEXT(
    VkDevice                      _device,
    VkIndirectExecutionSetEXT     indirectExecutionSet,
    const VkAllocationCallbacks*  pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   VK_FROM_HANDLE(lvp_indirect_execution_set, iset, indirectExecutionSet);

   if (!iset)
      return;

   vk_object_base_finish(&iset->base);
   vk_free2(&device->vk.alloc, pAllocator, iset);
}

VKAPI_ATTR void VKAPI_CALL lvp_UpdateIndirectExecutionSetPipelineEXT(
    VkDevice                              device,
    VkIndirectExecutionSetEXT             indirectExecutionSet,
    uint32_t                              executionSetWriteCount,
    const VkWriteIndirectExecutionSetPipelineEXT* pExecutionSetWrites)
{
   VK_FROM_HANDLE(lvp_indirect_execution_set, iset, indirectExecutionSet);

   assert(!iset->is_shaders);
   for (unsigned i = 0; i < executionSetWriteCount; i++) {
      iset->array[pExecutionSetWrites[i].index] = pExecutionSetWrites[i].pipeline;
   }
}

VKAPI_ATTR void VKAPI_CALL lvp_UpdateIndirectExecutionSetShaderEXT(
    VkDevice                              device,
    VkIndirectExecutionSetEXT             indirectExecutionSet,
    uint32_t                              executionSetWriteCount,
    const VkWriteIndirectExecutionSetShaderEXT* pExecutionSetWrites)
{
   VK_FROM_HANDLE(lvp_indirect_execution_set, iset, indirectExecutionSet);

   assert(iset->is_shaders);
   for (unsigned i = 0; i < executionSetWriteCount; i++) {
      iset->array[pExecutionSetWrites[i].index] = pExecutionSetWrites[i].shader;
   }
}

static size_t
get_token_info_size(VkIndirectCommandsTokenTypeEXT type)
{
   switch (type) {
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
      return sizeof(VkIndirectCommandsVertexBufferTokenEXT);
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT:
      return sizeof(VkIndirectCommandsPushConstantTokenEXT);
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT:
      return sizeof(VkIndirectCommandsIndexBufferTokenEXT);
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT:
      return sizeof(VkIndirectCommandsExecutionSetTokenEXT);
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_COUNT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_NV_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT:
      return 0;
   default: break;
   }
   unreachable("unknown token type");
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateIndirectCommandsLayoutEXT(
    VkDevice                                     _device,
    const VkIndirectCommandsLayoutCreateInfoEXT* pCreateInfo,
    const VkAllocationCallbacks*                 pAllocator,
    VkIndirectCommandsLayoutEXT*                 pIndirectCommandsLayout)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_indirect_command_layout_ext *elayout;
   size_t token_size = pCreateInfo->tokenCount * sizeof(VkIndirectCommandsLayoutTokenEXT);

   for (unsigned i = 0; i < pCreateInfo->tokenCount; i++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &pCreateInfo->pTokens[i];
      token_size += get_token_info_size(token->type);
   }

   elayout = vk_indirect_command_layout_create(&device->vk, pCreateInfo, pAllocator, sizeof(struct lvp_indirect_command_layout_ext) + token_size);
   if (!elayout)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   enum lvp_indirect_layout_type type = LVP_INDIRECT_COMMAND_LAYOUT_DRAW;

   for (unsigned i = 0; i < pCreateInfo->tokenCount; i++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &pCreateInfo->pTokens[i];
      switch (token->type) {
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT:
         type = LVP_INDIRECT_COMMAND_LAYOUT_DRAW;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_COUNT_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_NV_EXT:
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_EXT:
         type = LVP_INDIRECT_COMMAND_LAYOUT_DRAW_COUNT;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT:
         type = LVP_INDIRECT_COMMAND_LAYOUT_DISPATCH;
         break;
      case VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT:
         type = LVP_INDIRECT_COMMAND_LAYOUT_RAYS;
         break;
      default: break;
      }
   }
   elayout->type = type;

   /* tokens are the last member of the struct */
   size_t tokens_offset = sizeof(struct lvp_indirect_command_layout_ext) + pCreateInfo->tokenCount * sizeof(VkIndirectCommandsLayoutTokenEXT);
   typed_memcpy(elayout->tokens, pCreateInfo->pTokens, pCreateInfo->tokenCount);
   uint8_t *ptr = ((uint8_t *)elayout) + tokens_offset;
   /* after the tokens comes the token data */
   for (unsigned i = 0; i < pCreateInfo->tokenCount; i++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &pCreateInfo->pTokens[i];
      size_t tsize = get_token_info_size(token->type);
      if (tsize) {
         elayout->tokens[i].data.pPushConstant = (void*)ptr;
         memcpy(ptr, token->data.pPushConstant, tsize);
      }
      ptr += tsize;
   }

   *pIndirectCommandsLayout = lvp_indirect_command_layout_ext_to_handle(elayout);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL lvp_DestroyIndirectCommandsLayoutEXT(
    VkDevice                                    _device,
    VkIndirectCommandsLayoutEXT                  indirectCommandsLayout,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   VK_FROM_HANDLE(lvp_indirect_command_layout_ext, elayout, indirectCommandsLayout);

   if (!elayout)
      return;

   vk_indirect_command_layout_destroy(&device->vk, pAllocator, &elayout->vk);
}


enum vk_cmd_type
lvp_ext_dgc_token_to_cmd_type(const struct lvp_indirect_command_layout_ext *elayout, const VkIndirectCommandsLayoutTokenEXT *token)
{
   switch (token->type) {
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
      return VK_CMD_BIND_VERTEX_BUFFERS2;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT:
      return VK_CMD_PUSH_CONSTANTS2_KHR;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT:
      return VK_CMD_BIND_INDEX_BUFFER2_KHR;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT:
      return elayout->vk.is_shaders ? VK_CMD_BIND_SHADERS_EXT : VK_CMD_BIND_PIPELINE;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT:
      return VK_CMD_DRAW_INDEXED;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT:
      return VK_CMD_DRAW;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_COUNT_EXT:
      return VK_CMD_DRAW_INDEXED_INDIRECT;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT:
      return VK_CMD_DRAW_INDIRECT;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT:
      return VK_CMD_DISPATCH;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT:
      return VK_CMD_TRACE_RAYS_KHR;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_NV_EXT:
      unreachable("unsupported NV mesh");
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT:
      return VK_CMD_DRAW_MESH_TASKS_EXT;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_EXT:
      return VK_CMD_DRAW_MESH_TASKS_INDIRECT_EXT;
   default:
      unreachable("unknown token type");
   }
   return UINT32_MAX;
}

size_t
lvp_ext_dgc_token_size(const struct lvp_indirect_command_layout_ext *elayout, const VkIndirectCommandsLayoutTokenEXT *token)
{
   UNUSED struct vk_cmd_queue_entry *cmd;
   enum vk_cmd_type type = lvp_ext_dgc_token_to_cmd_type(elayout, token);
   size_t size = vk_cmd_queue_type_sizes[type];
   if (token->type == VK_INDIRECT_COMMANDS_TOKEN_TYPE_PUSH_CONSTANT_EXT || token->type == VK_INDIRECT_COMMANDS_TOKEN_TYPE_SEQUENCE_INDEX_EXT) {
      size += sizeof(*cmd->u.push_constants2_khr.push_constants_info);
      size += token->data.pPushConstant->updateRange.size;
      return size;
   }
   if (token->type == VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT) {
      /* special case: switch between pipelines/shaders */
      /* CmdBindShaders has 2 dynamically sized arrays */
      if (elayout->vk.is_shaders)
         size += sizeof(int64_t) * util_bitcount(token->data.pExecutionSet->shaderStages) * 2;
      return size;
   }

   if (token->type == VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT)
      return size + sizeof(VkStridedDeviceAddressRegionKHR) * 4;

   switch (token->type) {
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_VERTEX_BUFFER_EXT:
      size += sizeof(*cmd->u.bind_vertex_buffers.buffers);
      size += sizeof(*cmd->u.bind_vertex_buffers.offsets);
      size += sizeof(*cmd->u.bind_vertex_buffers2.sizes) + sizeof(*cmd->u.bind_vertex_buffers2.strides);
      break;
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_INDEX_BUFFER_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_EXECUTION_SET_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_INDEXED_COUNT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_COUNT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DISPATCH_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_NV_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_NV_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_DRAW_MESH_TASKS_COUNT_EXT:
   case VK_INDIRECT_COMMANDS_TOKEN_TYPE_TRACE_RAYS2_EXT:
      break;
   default:
      unreachable("unknown type!");
   }
   return size;
}

VKAPI_ATTR void VKAPI_CALL lvp_GetGeneratedCommandsMemoryRequirementsEXT(
    VkDevice                                    device,
    const VkGeneratedCommandsMemoryRequirementsInfoEXT* pInfo,
    VkMemoryRequirements2*                      pMemoryRequirements)
{
   VK_FROM_HANDLE(lvp_indirect_command_layout_ext, elayout, pInfo->indirectCommandsLayout);

   size_t size = sizeof(struct list_head);

   for (unsigned i = 0; i < elayout->vk.token_count; i++) {
      const VkIndirectCommandsLayoutTokenEXT *token = &elayout->tokens[i];
      size += lvp_ext_dgc_token_size(elayout, token);
   }
   if (elayout->type == LVP_INDIRECT_COMMAND_LAYOUT_DRAW || elayout->type == LVP_INDIRECT_COMMAND_LAYOUT_DRAW_COUNT)
      /* set/unset indirect draw offset */
      size += sizeof(struct vk_cmd_queue_entry) * (pInfo->maxSequenceCount + 1);

   size *= pInfo->maxSequenceCount;

   pMemoryRequirements->memoryRequirements.memoryTypeBits = 1;
   pMemoryRequirements->memoryRequirements.alignment = 4;
   pMemoryRequirements->memoryRequirements.size = align(size, pMemoryRequirements->memoryRequirements.alignment);
}
