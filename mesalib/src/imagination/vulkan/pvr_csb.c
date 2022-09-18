/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv_cl.c which is:
 * Copyright © 2019 Raspberry Pi
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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_device_info.h"
#include "pvr_private.h"
#include "vk_log.h"

/**
 * \file pvr_csb.c
 *
 * \brief Contains functions to manage Control Stream Builder (csb) object.
 *
 * A csb object can be used to create a primary/main control stream, referred
 * as control stream hereafter, or a secondary control stream, also referred as
 * a sub control stream. The main difference between these is that, the control
 * stream is the one directly submitted to the GPU and is terminated using
 * STREAM_TERMINATE. Whereas, the secondary control stream can be thought of as
 * an independent set of commands that can be referenced by a primary control
 * stream to avoid duplication and is instead terminated using STREAM_RETURN,
 * which means the control stream parser should return to the main stream it
 * came from.
 *
 * Note: Sub control stream is only supported for PVR_CMD_STREAM_TYPE_GRAPHICS
 * type control streams.
 */

/**
 * \brief Size of the individual csb buffer object.
 */
#define PVR_CMD_BUFFER_CSB_BO_SIZE 4096

/**
 * \brief Initializes the csb object.
 *
 * \param[in] device Logical device pointer.
 * \param[in] csb    Control Stream Builder object to initialize.
 *
 * \sa #pvr_csb_finish()
 */
void pvr_csb_init(struct pvr_device *device,
                  enum pvr_cmd_stream_type stream_type,
                  struct pvr_csb *csb)
{
   csb->start = NULL;
   csb->next = NULL;
   csb->pvr_bo = NULL;
   csb->end = NULL;
   csb->device = device;
   csb->stream_type = stream_type;
   csb->status = VK_SUCCESS;
   list_inithead(&csb->pvr_bo_list);
}

/**
 * \brief Frees the resources associated with the csb object.
 *
 * \param[in] csb Control Stream Builder object to free.
 *
 * \sa #pvr_csb_init()
 */
void pvr_csb_finish(struct pvr_csb *csb)
{
   list_for_each_entry_safe (struct pvr_bo, pvr_bo, &csb->pvr_bo_list, link) {
      list_del(&pvr_bo->link);
      pvr_bo_free(csb->device, pvr_bo);
   }

   /* Leave the csb in a reset state to catch use after destroy instances */
   pvr_csb_init(NULL, PVR_CMD_STREAM_TYPE_INVALID, csb);
}

/**
 * \brief Helper function to extend csb memory.
 *
 * Allocates a new buffer object and links it with the previous buffer object
 * using STREAM_LINK dwords and updates csb object to use the new buffer.
 *
 * To make sure that we have enough space to emit STREAM_LINK dwords in the
 * current buffer, a few bytes are reserved at the end, every time a buffer is
 * created. Every time we allocate a new buffer we fix the current buffer in use
 * to emit the stream link dwords. This makes sure that when
 * #pvr_csb_alloc_dwords() is called from #pvr_csb_emit() to add STREAM_LINK0
 * and STREAM_LINK1, it succeeds without trying to allocate new pages.
 *
 * \param[in] csb Control Stream Builder object to extend.
 * \return true on success and false otherwise.
 */
static bool pvr_csb_buffer_extend(struct pvr_csb *csb)
{
   const uint8_t stream_link_space = (pvr_cmd_length(VDMCTRL_STREAM_LINK0) +
                                      pvr_cmd_length(VDMCTRL_STREAM_LINK1)) *
                                     4;
   const uint32_t cache_line_size =
      rogue_get_slc_cache_line_size(&csb->device->pdevice->dev_info);
   struct pvr_bo *pvr_bo;
   VkResult result;

   /* Make sure extra space allocated for stream links is sufficient for both
    * stream types.
    */
   STATIC_ASSERT((pvr_cmd_length(VDMCTRL_STREAM_LINK0) +
                  pvr_cmd_length(VDMCTRL_STREAM_LINK1)) ==
                 (pvr_cmd_length(CDMCTRL_STREAM_LINK0) +
                  pvr_cmd_length(CDMCTRL_STREAM_LINK1)));

   result = pvr_bo_alloc(csb->device,
                         csb->device->heaps.general_heap,
                         PVR_CMD_BUFFER_CSB_BO_SIZE,
                         cache_line_size,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &pvr_bo);
   if (result != VK_SUCCESS) {
      vk_error(csb->device, result);
      csb->status = result;
      return false;
   }

   /* Chain to the old BO if this is not the first BO in csb */
   if (csb->pvr_bo) {
      csb->end += stream_link_space;
      assert(csb->next + stream_link_space <= csb->end);

      switch (csb->stream_type) {
      case PVR_CMD_STREAM_TYPE_GRAPHICS:
         pvr_csb_emit (csb, VDMCTRL_STREAM_LINK0, link) {
            link.link_addrmsb = pvr_bo->vma->dev_addr;
         }

         pvr_csb_emit (csb, VDMCTRL_STREAM_LINK1, link) {
            link.link_addrlsb = pvr_bo->vma->dev_addr;
         }

         break;

      case PVR_CMD_STREAM_TYPE_COMPUTE:
         pvr_csb_emit (csb, CDMCTRL_STREAM_LINK0, link) {
            link.link_addrmsb = pvr_bo->vma->dev_addr;
         }

         pvr_csb_emit (csb, CDMCTRL_STREAM_LINK1, link) {
            link.link_addrlsb = pvr_bo->vma->dev_addr;
         }

         break;

      default:
         unreachable("Unknown stream type");
         break;
      }
   }

   csb->pvr_bo = pvr_bo;
   csb->start = pvr_bo->bo->map;

   /* Reserve stream link size at the end to make sure we don't run out of
    * space when a stream link is required.
    */
   csb->end = csb->start + pvr_bo->bo->size - stream_link_space;
   csb->next = csb->start;

   list_addtail(&pvr_bo->link, &csb->pvr_bo_list);

   return true;
}

/**
 * \brief Provides a chunk of memory from the current csb buffer. In cases where
 * the buffer is not able to fulfill the required amount of memory,
 * #pvr_csb_buffer_extend() is called to allocate a new buffer. Maximum size
 * allocable in bytes is #PVR_CMD_BUFFER_CSB_BO_SIZE - size of STREAM_LINK0
 * and STREAM_LINK1 dwords.
 *
 * \param[in] csb        Control Stream Builder object to allocate from.
 * \param[in] num_dwords Number of dwords to allocate.
 * \return Valid host virtual address or NULL otherwise.
 */
void *pvr_csb_alloc_dwords(struct pvr_csb *csb, uint32_t num_dwords)
{
   const uint32_t required_space = num_dwords * 4;

   if (csb->status != VK_SUCCESS)
      return NULL;

   if (csb->next + required_space > csb->end) {
      bool ret = pvr_csb_buffer_extend(csb);
      if (!ret)
         return NULL;
   }

   void *p = csb->next;

   csb->next += required_space;
   assert(csb->next <= csb->end);

   return p;
}

/**
 * \brief Adds VDMCTRL_STREAM_RETURN dword into the control stream pointed by
 * csb object. Given a VDMCTRL_STREAM_RETURN marks the end of the sub control
 * stream, we return the status of the control stream as well.
 *
 * \param[in] csb Control Stream Builder object to add VDMCTRL_STREAM_RETURN to.
 * \return VK_SUCCESS on success, or error code otherwise.
 */
VkResult pvr_csb_emit_return(struct pvr_csb *csb)
{
   /* STREAM_RETURN is only supported by graphics control stream. */
   assert(csb->stream_type == PVR_CMD_STREAM_TYPE_GRAPHICS);

   /* clang-format off */
   pvr_csb_emit(csb, VDMCTRL_STREAM_RETURN, ret);
   /* clang-format on */

   return csb->status;
}

/**
 * \brief Adds STREAM_TERMINATE dword into the control stream pointed by csb
 * object. Given a STREAM_TERMINATE marks the end of the control stream, we
 * return the status of the control stream as well.
 *
 * \param[in] csb Control Stream Builder object to terminate.
 * \return VK_SUCCESS on success, or error code otherwise.
 */
VkResult pvr_csb_emit_terminate(struct pvr_csb *csb)
{
   switch (csb->stream_type) {
   case PVR_CMD_STREAM_TYPE_GRAPHICS:
      /* clang-format off */
      pvr_csb_emit(csb, VDMCTRL_STREAM_TERMINATE, terminate);
      /* clang-format on */
      break;

   case PVR_CMD_STREAM_TYPE_COMPUTE:
      /* clang-format off */
      pvr_csb_emit(csb, CDMCTRL_STREAM_TERMINATE, terminate);
      /* clang-format on */
      break;

   default:
      unreachable("Unknown stream type");
      break;
   }

   return csb->status;
}
