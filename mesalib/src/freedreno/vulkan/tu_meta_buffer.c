#include "tu_private.h"
#include "tu_blit.h"
#include "tu_cs.h"

void
tu_CmdFillBuffer(VkCommandBuffer commandBuffer,
                 VkBuffer dstBuffer,
                 VkDeviceSize dstOffset,
                 VkDeviceSize fillSize,
                 uint32_t data)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, dstBuffer);

   if (fillSize == VK_WHOLE_SIZE)
      fillSize = buffer->size - dstOffset;

   tu_bo_list_add(&cmd->bo_list, buffer->bo, MSM_SUBMIT_BO_WRITE);

   tu_blit(cmd, &(struct tu_blit) {
      .dst = {
         .fmt = VK_FORMAT_R32_UINT,
         .va = tu_buffer_iova(buffer) + dstOffset,
         .width = fillSize / 4,
         .height = 1,
         .samples = 1,
      },
      .layers = 1,
      .clear_value[0] = data,
      .type = TU_BLIT_CLEAR,
      .buffer = true,
   });
}

void
tu_CmdUpdateBuffer(VkCommandBuffer commandBuffer,
                   VkBuffer dstBuffer,
                   VkDeviceSize dstOffset,
                   VkDeviceSize dataSize,
                   const void *pData)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, dstBuffer);

   tu_bo_list_add(&cmd->bo_list, buffer->bo, MSM_SUBMIT_BO_WRITE);

   struct ts_cs_memory tmp;
   VkResult result = tu_cs_alloc(cmd->device, &cmd->sub_cs, DIV_ROUND_UP(dataSize, 64), 64, &tmp);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   memcpy(tmp.map, pData, dataSize);

   tu_blit(cmd, &(struct tu_blit) {
      .dst = {
         .fmt = VK_FORMAT_R32_UINT,
         .va = tu_buffer_iova(buffer) + dstOffset,
         .width = dataSize / 4,
         .height = 1,
         .samples = 1,
      },
      .src = {
         .fmt = VK_FORMAT_R32_UINT,
         .va = tmp.iova,
         .width = dataSize / 4,
         .height = 1,
         .samples = 1,
      },
      .layers = 1,
      .type = TU_BLIT_COPY,
      .buffer = true,
   });
}
