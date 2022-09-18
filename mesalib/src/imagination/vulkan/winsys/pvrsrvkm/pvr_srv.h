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

#ifndef PVR_SRV_H
#define PVR_SRV_H

#include <stdint.h>
#include <pthread.h>
#include <vulkan/vulkan.h>

#include "pvr_winsys.h"
#include "util/macros.h"
#include "util/vma.h"

/*******************************************
   Misc defines
 *******************************************/

/* 64KB is MAX anticipated OS page size */
#define PVR_SRV_RESERVED_SIZE_GRANULARITY 0x10000

#define PVR_SRV_DEVMEM_HEAPNAME_MAXLENGTH 160

#define PVR_SRV_GENERAL_HEAP_IDENT "General"
#define PVR_SRV_PDSCODEDATA_HEAP_IDENT "PDS Code and Data"
#define PVR_SRV_RGNHDR_BRN_63142_HEAP_IDENT "RgnHdr BRN63142"
#define PVR_SRV_TRANSFER_3D_HEAP_IDENT "TQ3DParameters"
#define PVR_SRV_USCCODE_HEAP_IDENT "USC Code"
#define PVR_SRV_VISIBILITY_TEST_HEAP_IDENT "Visibility Test"

#define FWIF_PDS_HEAP_TOTAL_BYTES 4096
#define FWIF_PDS_HEAP_VDM_SYNC_OFFSET_BYTES 0
#define FWIF_PDS_HEAP_EOT_OFFSET_BYTES 128
#define FWIF_GENERAL_HEAP_TOTAL_BYTES 4096
#define FWIF_USC_HEAP_TOTAL_BYTES 4096
#define FWIF_USC_HEAP_VDM_SYNC_OFFSET_BYTES 0
#define FWIF_GENERAL_HEAP_YUV_CSC_OFFSET_BYTES 128U

/*******************************************
    structure definitions
 *******************************************/
struct pvr_srv_winsys_heap {
   struct pvr_winsys_heap base;

   void *server_heap;
};

struct pvr_srv_winsys {
   struct pvr_winsys base;

   int master_fd;
   int render_fd;

   const VkAllocationCallbacks *alloc;

   /* Packed bvnc */
   uint64_t bvnc;

   void *server_memctx;
   void *server_memctx_data;

   /* Required heaps */
   struct pvr_srv_winsys_heap general_heap;
   struct pvr_srv_winsys_heap pds_heap;
   struct pvr_srv_winsys_heap transfer_3d_heap;
   struct pvr_srv_winsys_heap usc_heap;
   struct pvr_srv_winsys_heap vis_test_heap;

   /* Optional heaps */
   bool rgn_hdr_heap_present;
   struct pvr_srv_winsys_heap rgn_hdr_heap;

   /* vma's for reserved memory regions */
   struct pvr_winsys_vma *pds_vma;
   struct pvr_winsys_vma *usc_vma;
   struct pvr_winsys_vma *general_vma;

   /* Sync block used for allocating sync primitives. */
   void *sync_block_handle;
   uint32_t sync_block_size;
   uint32_t sync_block_fw_addr;
   uint16_t sync_block_offset;
};

struct pvr_srv_sync_prim {
   struct pvr_srv_winsys *srv_ws;
   uint32_t offset;
   uint32_t value;
};

/*******************************************
    helper macros
 *******************************************/

#define to_pvr_srv_winsys(ws) container_of((ws), struct pvr_srv_winsys, base)
#define to_pvr_srv_winsys_heap(heap) \
   container_of((heap), struct pvr_srv_winsys_heap, base)

/*******************************************
    functions
 *******************************************/

struct pvr_srv_sync_prim *
pvr_srv_sync_prim_alloc(struct pvr_srv_winsys *srv_ws);
void pvr_srv_sync_prim_free(struct pvr_srv_sync_prim *sync_prim);

static inline uint32_t
pvr_srv_sync_prim_get_fw_addr(const struct pvr_srv_sync_prim *const sync_prim)
{
   return sync_prim->srv_ws->sync_block_fw_addr + sync_prim->offset;
}

#endif /* PVR_SRV_H */
