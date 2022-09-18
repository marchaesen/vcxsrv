/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv_cl.h which is:
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

#ifndef PVR_CSB_H
#define PVR_CSB_H

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_bo.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "util/list.h"
#include "util/macros.h"

#define __pvr_address_type pvr_dev_addr_t
#define __pvr_get_address(pvr_dev_addr) (pvr_dev_addr).addr
/* clang-format off */
#define __pvr_make_address(addr_u64) PVR_DEV_ADDR(addr_u64)
/* clang-format on */

#include "csbgen/rogue_hwdefs.h"

struct pvr_device;

enum pvr_cmd_stream_type {
   PVR_CMD_STREAM_TYPE_INVALID = 0, /* explicitly treat 0 as invalid */
   PVR_CMD_STREAM_TYPE_GRAPHICS,
   PVR_CMD_STREAM_TYPE_COMPUTE,
};

struct pvr_csb {
   struct pvr_device *device;

   /* Pointer to current csb buffer object */
   struct pvr_bo *pvr_bo;

   /* pointers to current bo memory */
   void *start;
   void *end;
   void *next;

   /* List of csb buffer objects */
   struct list_head pvr_bo_list;

   enum pvr_cmd_stream_type stream_type;

   /* Current error status of the command buffer. Used to track inconsistent
    * or incomplete command buffer states that are the consequence of run-time
    * errors such as out of memory scenarios. We want to track this in the
    * csb because the command buffer object is not visible to some parts
    * of the driver.
    */
   VkResult status;
};

/**
 * \brief Gets the status of the csb.
 *
 * \param[in] csb Control Stream Builder object.
 * \return VK_SUCCESS if the csb hasn't encountered any error or error code
 *         otherwise.
 */
static inline VkResult pvr_csb_get_status(const struct pvr_csb *csb)
{
   return csb->status;
}

/**
 * \brief Checks if the control stream is empty or not.
 *
 * \param[in] csb Control Stream Builder object.
 * \return true if csb is empty false otherwise.
 */
static inline bool pvr_csb_is_empty(const struct pvr_csb *csb)
{
   return list_is_empty(&csb->pvr_bo_list);
}

static inline pvr_dev_addr_t
pvr_csb_get_start_address(const struct pvr_csb *csb)
{
   if (!pvr_csb_is_empty(csb)) {
      struct pvr_bo *pvr_bo =
         list_first_entry(&csb->pvr_bo_list, struct pvr_bo, link);

      return pvr_bo->vma->dev_addr;
   }

   return PVR_DEV_ADDR_INVALID;
}

void pvr_csb_init(struct pvr_device *device,
                  enum pvr_cmd_stream_type stream_type,
                  struct pvr_csb *csb);
void pvr_csb_finish(struct pvr_csb *csb);
void *pvr_csb_alloc_dwords(struct pvr_csb *csb, uint32_t num_dwords);
VkResult pvr_csb_emit_return(struct pvr_csb *csb);
VkResult pvr_csb_emit_terminate(struct pvr_csb *csb);

#define PVRX(x) ROGUE_##x
#define pvr_cmd_length(x) PVRX(x##_length)
#define pvr_cmd_header(x) PVRX(x##_header)
#define pvr_cmd_pack(x) PVRX(x##_pack)

/**
 * \brief Packs a command/state into one or more dwords and stores them in the
 * memory pointed to by _dst.
 *
 * \param[out] _dst    Pointer to store the packed command/state.
 * \param[in] cmd      Command/state type.
 * \param[in,out] name Name to give to the command/state structure variable,
 *                     which contains the information to be packed and emitted.
 *                     This can be used by the caller to modify the command or
 *                     state information before it's packed.
 */
#define pvr_csb_pack(_dst, cmd, name)                                 \
   for (struct PVRX(cmd) name = { pvr_cmd_header(cmd) },              \
                         *_loop_terminate = &name;                    \
        __builtin_expect(_loop_terminate != NULL, 1);                 \
        ({                                                            \
           STATIC_ASSERT(sizeof(*(_dst)) == pvr_cmd_length(cmd) * 4); \
           pvr_cmd_pack(cmd)((_dst), &name);                          \
           _loop_terminate = NULL;                                    \
        }))

/**
 * \brief Merges dwords0 and dwords1 arrays and stores the result into the
 * control stream pointed by the csb object.
 *
 * \param[in] csb     Control Stream Builder object.
 * \param[in] dwords0 Dwords0 array.
 * \param[in] dwords1 Dwords1 array.
 */
#define pvr_csb_emit_merge(csb, dwords0, dwords1)                \
   do {                                                          \
      uint32_t *dw;                                              \
      STATIC_ASSERT(ARRAY_SIZE(dwords0) == ARRAY_SIZE(dwords1)); \
      dw = pvr_csb_alloc_dwords(csb, ARRAY_SIZE(dwords0));       \
      if (!dw)                                                   \
         break;                                                  \
      for (uint32_t i = 0; i < ARRAY_SIZE(dwords0); i++)         \
         dw[i] = (dwords0)[i] | (dwords1)[i];                    \
   } while (0)

/**
 * \brief Packs a command/state into one or more dwords and stores them into
 * the control stream pointed by the csb object.
 *
 * \param[in] csb      Control Stream Builder object.
 * \param[in] cmd      Command/state type.
 * \param[in,out] name Name to give to the command/state structure variable,
 *                     which contains the information to be packed. This can be
 *                     used by the caller to modify the command or state
 *                     information before it's packed.
 */
#define pvr_csb_emit(csb, cmd, name)                               \
   for (struct PVRX(cmd)                                           \
           name = { pvr_cmd_header(cmd) },                         \
           *_dst = pvr_csb_alloc_dwords(csb, pvr_cmd_length(cmd)); \
        __builtin_expect(_dst != NULL, 1);                         \
        ({                                                         \
           pvr_cmd_pack(cmd)(_dst, &name);                         \
           _dst = NULL;                                            \
        }))

/**
 * \brief Stores dword into the control stream pointed by the csb object.
 *
 * \param[in] csb   Control Stream Builder object.
 * \param[in] dword Dword to store into control stream.
 */
#define pvr_csb_emit_dword(csb, dword)                  \
   do {                                                 \
      uint32_t *dw;                                     \
      STATIC_ASSERT(sizeof(dword) == sizeof(uint32_t)); \
      dw = pvr_csb_alloc_dwords(csb, 1U);               \
      if (!dw)                                          \
         break;                                         \
      *dw = dword;                                      \
   } while (0)

#endif /* PVR_CSB_H */
