/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_alloc.h"
#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"

mali_ptr
panvk_per_arch(cmd_prepare_push_uniforms)(struct panvk_cmd_buffer *cmdbuf,
                                          void *sysvals, unsigned sysvals_sz)
{
   struct panfrost_ptr push_uniforms =
      panvk_cmd_alloc_dev_mem(cmdbuf, desc, 512, 16);

   if (push_uniforms.gpu) {
      /* The first half is used for push constants. */
      memcpy(push_uniforms.cpu, cmdbuf->state.push_constants.data,
             sizeof(cmdbuf->state.push_constants.data));

      /* The second half is used for sysvals. */
      memcpy((uint8_t *)push_uniforms.cpu + 256, sysvals, sysvals_sz);
   }

   return push_uniforms.gpu;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdPushConstants2KHR)(
   VkCommandBuffer commandBuffer,
   const VkPushConstantsInfoKHR *pPushConstantsInfo)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_ALL_GRAPHICS)
      cmdbuf->state.gfx.push_uniforms = 0;

   if (pPushConstantsInfo->stageFlags & VK_SHADER_STAGE_COMPUTE_BIT)
      cmdbuf->state.compute.push_uniforms = 0;

   memcpy(cmdbuf->state.push_constants.data + pPushConstantsInfo->offset,
          pPushConstantsInfo->pValues, pPushConstantsInfo->size);
}
