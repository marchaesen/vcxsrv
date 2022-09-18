/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <vulkan/vulkan.h>

#include "pvr_job_common.h"
#include "pvr_job_context.h"
#include "pvr_job_transfer.h"
#include "pvr_private.h"
#include "pvr_winsys.h"
#include "util/list.h"
#include "util/macros.h"
#include "vk_sync.h"

/* FIXME: Implement gpu based transfer support. */
VkResult pvr_transfer_job_submit(struct pvr_device *device,
                                 struct pvr_transfer_ctx *ctx,
                                 struct pvr_sub_cmd_transfer *sub_cmd,
                                 struct vk_sync **waits,
                                 uint32_t wait_count,
                                 uint32_t *stage_flags,
                                 struct vk_sync *signal_sync)
{
   /* Wait for transfer semaphores here before doing any transfers. */
   for (uint32_t i = 0U; i < wait_count; i++) {
      if (stage_flags[i] & PVR_PIPELINE_STAGE_TRANSFER_BIT) {
         VkResult result = vk_sync_wait(&device->vk,
                                        waits[i],
                                        0U,
                                        VK_SYNC_WAIT_COMPLETE,
                                        UINT64_MAX);
         if (result != VK_SUCCESS)
            return result;

         stage_flags[i] &= ~PVR_PIPELINE_STAGE_TRANSFER_BIT;
      }
   }

   list_for_each_entry_safe (struct pvr_transfer_cmd,
                             transfer_cmd,
                             &sub_cmd->transfer_cmds,
                             link) {
      bool src_mapped = false;
      bool dst_mapped = false;
      void *src_addr;
      void *dst_addr;
      void *ret_ptr;

      /* Map if bo is not mapped. */
      if (!transfer_cmd->src->vma->bo->map) {
         src_mapped = true;
         ret_ptr = device->ws->ops->buffer_map(transfer_cmd->src->vma->bo);
         if (!ret_ptr)
            return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
      }

      if (!transfer_cmd->dst->vma->bo->map) {
         dst_mapped = true;
         ret_ptr = device->ws->ops->buffer_map(transfer_cmd->dst->vma->bo);
         if (!ret_ptr)
            return vk_error(device, VK_ERROR_MEMORY_MAP_FAILED);
      }

      src_addr =
         transfer_cmd->src->vma->bo->map + transfer_cmd->src->vma->bo_offset;
      dst_addr =
         transfer_cmd->dst->vma->bo->map + transfer_cmd->dst->vma->bo_offset;

      for (uint32_t i = 0; i < transfer_cmd->region_count; i++) {
         VkBufferCopy2 *region = &transfer_cmd->regions[i];

         memcpy(dst_addr + region->dstOffset,
                src_addr + region->srcOffset,
                region->size);
      }

      if (src_mapped)
         device->ws->ops->buffer_unmap(transfer_cmd->src->vma->bo);

      if (dst_mapped)
         device->ws->ops->buffer_unmap(transfer_cmd->dst->vma->bo);
   }

   /* Given we are doing CPU based copy, completion fence should always be
    * signaled. This should be fixed when GPU based copy is implemented.
    */
   return vk_sync_signal(&device->vk, signal_sync, 0);
}
