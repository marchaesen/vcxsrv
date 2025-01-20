/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"

VkResult
panvk_per_arch(cmd_prepare_push_uniforms)(struct panvk_cmd_buffer *cmdbuf,
                                          const struct panvk_shader *shader)
{
   uint64_t *push_ptr;

   switch (shader->vk.stage) {
   case MESA_SHADER_COMPUTE:
      if (!compute_state_dirty(cmdbuf, PUSH_UNIFORMS))
         return VK_SUCCESS;
      push_ptr = &cmdbuf->state.compute.push_uniforms;
      break;
   case MESA_SHADER_VERTEX:
      if (!gfx_state_dirty(cmdbuf, VS_PUSH_UNIFORMS))
         return VK_SUCCESS;
      push_ptr = &cmdbuf->state.gfx.vs.push_uniforms;
      break;
   case MESA_SHADER_FRAGMENT:
      if (!gfx_state_dirty(cmdbuf, FS_PUSH_UNIFORMS))
         return VK_SUCCESS;
      push_ptr = &cmdbuf->state.gfx.fs.push_uniforms;
      break;
   default:
      assert(!"Invalid stage");
      return VK_SUCCESS;
   }

   if (!shader->fau.total_count) {
      *push_ptr = 0;
      return VK_SUCCESS;
   }

   struct panfrost_ptr push_uniforms = panvk_cmd_alloc_dev_mem(
      cmdbuf, desc, shader->fau.total_count * sizeof(uint64_t),
      sizeof(uint64_t));

   if (!push_uniforms.gpu)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   if (shader->vk.stage == MESA_SHADER_COMPUTE) {
      cmdbuf->state.compute.sysvals.push_consts =
         push_uniforms.gpu + (shader->fau.sysval_count * FAU_WORD_SIZE);
   } else {
      cmdbuf->state.gfx.sysvals.push_consts =
         push_uniforms.gpu + (shader->fau.sysval_count * FAU_WORD_SIZE);
   }

   uint64_t *sysvals = shader->vk.stage == MESA_SHADER_COMPUTE
                          ? (uint64_t *)&cmdbuf->state.compute.sysvals
                          : (uint64_t *)&cmdbuf->state.gfx.sysvals;
   uint64_t *push_consts = cmdbuf->state.push_constants.data;
   uint64_t *faus = push_uniforms.cpu;
   uint32_t w, fau = 0;

   /* After packing, the sysvals come first, followed by the user push constants.
    * The ordering is encoded shader side, so don't re-order these loops. */
   BITSET_FOREACH_SET(w, shader->fau.used_sysvals, MAX_SYSVAL_FAUS)
      faus[fau++] = sysvals[w];

   BITSET_FOREACH_SET(w, shader->fau.used_push_consts, MAX_PUSH_CONST_FAUS)
      faus[fau++] = push_consts[w];

   *push_ptr = push_uniforms.gpu;
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPushConstants2KHR)(
   VkCommandBuffer commandBuffer,
   const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_VERTEX_BIT)
      gfx_state_set_dirty(cmdbuf, VS_PUSH_UNIFORMS);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_FRAGMENT_BIT)
      gfx_state_set_dirty(cmdbuf, FS_PUSH_UNIFORMS);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      compute_state_set_dirty(cmdbuf, PUSH_UNIFORMS);

   uint8_t *data =
      (uint8_t *)cmdbuf->state.push_constants.data + pPushConstantsInfo->offset;

   memcpy(data, pPushConstantsInfo->pValues, pPushConstantsInfo->size);
}
