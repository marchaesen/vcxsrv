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

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>
#include <xf86drm.h>

#include "pvr_private.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "pvr_winsys_helper.h"
#include "util/u_atomic.h"
#include "vk_log.h"

int pvr_winsys_helper_display_buffer_create(int master_fd,
                                            uint64_t size,
                                            uint32_t *const handle_out)
{
   struct drm_mode_create_dumb args = {
      .width = size,
      .height = 1,
      .bpp = 8,
   };
   int ret;

   ret = drmIoctl(master_fd, DRM_IOCTL_MODE_CREATE_DUMB, &args);
   if (ret)
      return ret;

   *handle_out = args.handle;

   return 0;
}

int pvr_winsys_helper_display_buffer_destroy(int master_fd, uint32_t handle)
{
   struct drm_mode_destroy_dumb args = {
      .handle = handle,
   };

   return drmIoctl(master_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &args);
}

/* reserved_size can be 0 when no reserved area is needed. reserved_address must
 * be 0 if reserved_size is 0.
 */
VkResult pvr_winsys_helper_winsys_heap_init(
   struct pvr_winsys *const ws,
   pvr_dev_addr_t base_address,
   uint64_t size,
   pvr_dev_addr_t reserved_address,
   uint64_t reserved_size,
   uint32_t log2_page_size,
   const struct pvr_winsys_static_data_offsets *const static_data_offsets,
   struct pvr_winsys_heap *const heap)
{
   const bool reserved_area_bottom_of_heap = reserved_address.addr ==
                                             base_address.addr;
   const pvr_dev_addr_t vma_heap_begin_addr =
      reserved_area_bottom_of_heap
         ? PVR_DEV_ADDR_OFFSET(base_address, reserved_size)
         : base_address;
   const uint64_t vma_heap_size = size - reserved_size;

   assert(base_address.addr);
   assert(reserved_size <= size);

   /* As per the reserved_base powervr-km uapi documentation the reserved
    * region can only be at the beginning of the heap or at the end.
    * reserved_address is 0 if there is no reserved region.
    * pvrsrv-km doesn't explicitly provide this info and it's assumed that it's
    * always at the beginning.
    */
   assert(reserved_area_bottom_of_heap ||
          reserved_address.addr + reserved_size == base_address.addr + size ||
          (!reserved_address.addr && !reserved_size));

   heap->ws = ws;
   heap->base_addr = base_address;
   heap->reserved_addr = reserved_address;

   heap->size = size;
   heap->reserved_size = reserved_size;

   heap->page_size = 1 << log2_page_size;
   heap->log2_page_size = log2_page_size;

   util_vma_heap_init(&heap->vma_heap, vma_heap_begin_addr.addr, vma_heap_size);

   heap->vma_heap.alloc_high = false;

   /* It's expected that the heap destroy function to be the last thing that's
    * called, so we start the ref_count at 0.
    */
   p_atomic_set(&heap->ref_count, 0);

   if (pthread_mutex_init(&heap->lock, NULL))
      return vk_error(NULL, VK_ERROR_INITIALIZATION_FAILED);

   heap->static_data_offsets = *static_data_offsets;

   return VK_SUCCESS;
}

bool pvr_winsys_helper_winsys_heap_finish(struct pvr_winsys_heap *const heap)
{
   if (p_atomic_read(&heap->ref_count) != 0)
      return false;

   pthread_mutex_destroy(&heap->lock);
   util_vma_heap_finish(&heap->vma_heap);

   return true;
}

bool pvr_winsys_helper_heap_alloc(struct pvr_winsys_heap *const heap,
                                  uint64_t size,
                                  uint64_t alignment,
                                  struct pvr_winsys_vma *const vma_out)
{
   struct pvr_winsys_vma vma = {
      .heap = heap,
   };

   assert(util_is_power_of_two_nonzero(alignment));

   /* pvr_srv_winsys_buffer_create() page aligns the size. We must do the same
    * here to ensure enough heap space is allocated to be able to map the
    * buffer to the GPU.
    * We have to do this for the powervr kernel mode driver as well, as it
    * returns a page aligned size when allocating buffers.
    */
   alignment = MAX2(alignment, heap->page_size);

   size = ALIGN_POT(size, alignment);
   vma.size = size;

   pthread_mutex_lock(&heap->lock);
   vma.dev_addr =
      PVR_DEV_ADDR(util_vma_heap_alloc(&heap->vma_heap, size, heap->page_size));
   pthread_mutex_unlock(&heap->lock);

   if (!vma.dev_addr.addr) {
      vk_error(NULL, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      return false;
   }

   p_atomic_inc(&heap->ref_count);

   *vma_out = vma;

   return true;
}

void pvr_winsys_helper_heap_free(struct pvr_winsys_vma *const vma)
{
   struct pvr_winsys_heap *const heap = vma->heap;

   /* A vma with an existing device mapping should not be freed. */
   assert(!vma->bo);

   pthread_mutex_lock(&heap->lock);
   util_vma_heap_free(&heap->vma_heap, vma->dev_addr.addr, vma->size);
   pthread_mutex_unlock(&heap->lock);

   p_atomic_dec(&heap->ref_count);
}

/* Note: the function assumes the heap allocation in the reserved memory area
 * can be freed with the regular heap allocation free function. The free
 * function gets called on mapping failure.
 */
static VkResult
pvr_buffer_create_and_map(struct pvr_winsys *const ws,
                          heap_alloc_reserved_func heap_alloc_reserved,
                          struct pvr_winsys_heap *heap,
                          pvr_dev_addr_t dev_addr,
                          uint64_t size,
                          uint64_t alignment,
                          struct pvr_winsys_vma **const vma_out)
{
   struct pvr_winsys_vma *vma;
   struct pvr_winsys_bo *bo;
   pvr_dev_addr_t addr;
   VkResult result;

   /* Address should not be NULL, this function is used to allocate and map
    * reserved addresses and is only supposed to be used internally.
    */
   assert(dev_addr.addr);

   result = ws->ops->buffer_create(ws,
                                   size,
                                   alignment,
                                   PVR_WINSYS_BO_TYPE_GPU,
                                   PVR_WINSYS_BO_FLAG_CPU_ACCESS,
                                   &bo);
   if (result != VK_SUCCESS)
      return result;

   vma = heap_alloc_reserved(heap, dev_addr, size, alignment);
   if (!vma) {
      result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      goto err_pvr_winsys_buffer_destroy;
   }

   addr = ws->ops->vma_map(vma, bo, 0, size);
   if (!addr.addr) {
      result = VK_ERROR_MEMORY_MAP_FAILED;
      goto err_pvr_winsys_heap_free;
   }

   /* Note this won't destroy bo as its being used by VMA, once vma is
    * unmapped, bo will be destroyed automatically.
    */
   ws->ops->buffer_destroy(bo);

   *vma_out = vma;

   return VK_SUCCESS;

err_pvr_winsys_heap_free:
   ws->ops->heap_free(vma);

err_pvr_winsys_buffer_destroy:
   ws->ops->buffer_destroy(bo);

   return result;
}

static void inline pvr_buffer_destroy_and_unmap(struct pvr_winsys_vma *vma)
{
   const struct pvr_winsys *const ws = vma->heap->ws;

   /* Buffer object associated with the vma will be automatically destroyed
    * once vma is unmapped.
    */
   ws->ops->vma_unmap(vma);
   ws->ops->heap_free(vma);
}

VkResult pvr_winsys_helper_allocate_static_memory(
   struct pvr_winsys *const ws,
   heap_alloc_reserved_func heap_alloc_reserved,
   struct pvr_winsys_heap *const general_heap,
   struct pvr_winsys_heap *const pds_heap,
   struct pvr_winsys_heap *const usc_heap,
   struct pvr_winsys_vma **const general_vma_out,
   struct pvr_winsys_vma **const pds_vma_out,
   struct pvr_winsys_vma **const usc_vma_out)
{
   struct pvr_winsys_vma *general_vma;
   struct pvr_winsys_vma *pds_vma;
   struct pvr_winsys_vma *usc_vma;
   VkResult result;

   result = pvr_buffer_create_and_map(ws,
                                      heap_alloc_reserved,
                                      general_heap,
                                      general_heap->reserved_addr,
                                      general_heap->reserved_size,
                                      general_heap->page_size,
                                      &general_vma);
   if (result != VK_SUCCESS)
      return result;

   result = pvr_buffer_create_and_map(ws,
                                      heap_alloc_reserved,
                                      pds_heap,
                                      pds_heap->reserved_addr,
                                      pds_heap->reserved_size,
                                      pds_heap->page_size,
                                      &pds_vma);
   if (result != VK_SUCCESS)
      goto err_pvr_buffer_destroy_and_unmap_general;

   result = pvr_buffer_create_and_map(ws,
                                      heap_alloc_reserved,
                                      usc_heap,
                                      usc_heap->reserved_addr,
                                      pds_heap->reserved_size,
                                      usc_heap->page_size,
                                      &usc_vma);
   if (result != VK_SUCCESS)
      goto err_pvr_buffer_destroy_and_unmap_pds;

   *general_vma_out = general_vma;
   *pds_vma_out = pds_vma;
   *usc_vma_out = usc_vma;

   return VK_SUCCESS;

err_pvr_buffer_destroy_and_unmap_pds:
   pvr_buffer_destroy_and_unmap(pds_vma);

err_pvr_buffer_destroy_and_unmap_general:
   pvr_buffer_destroy_and_unmap(general_vma);

   return result;
}

void pvr_winsys_helper_free_static_memory(
   struct pvr_winsys_vma *const general_vma,
   struct pvr_winsys_vma *const pds_vma,
   struct pvr_winsys_vma *const usc_vma)
{
   pvr_buffer_destroy_and_unmap(usc_vma);
   pvr_buffer_destroy_and_unmap(pds_vma);
   pvr_buffer_destroy_and_unmap(general_vma);
}

static void pvr_setup_static_vdm_sync(uint8_t *const pds_ptr,
                                      uint64_t pds_sync_offset_in_bytes,
                                      uint8_t *const usc_ptr,
                                      uint64_t usc_sync_offset_in_bytes)
{
   /* TODO: this needs to be auto-generated */
   const uint8_t state_update[] = { 0x44, 0xA0, 0x80, 0x05,
                                    0x00, 0x00, 0x00, 0xFF };

   struct pvr_pds_kickusc_program ppp_state_update_program = { 0 };

   memcpy(usc_ptr + usc_sync_offset_in_bytes,
          state_update,
          sizeof(state_update));

   pvr_pds_setup_doutu(&ppp_state_update_program.usc_task_control,
                       usc_sync_offset_in_bytes,
                       0,
                       PVRX(PDSINST_DOUTU_SAMPLE_RATE_INSTANCE),
                       false);

   pvr_pds_kick_usc(&ppp_state_update_program,
                    (uint32_t *)&pds_ptr[pds_sync_offset_in_bytes],
                    0,
                    false,
                    PDS_GENERATE_CODEDATA_SEGMENTS);
}

static void
pvr_setup_static_pixel_event_program(uint8_t *const pds_ptr,
                                     uint64_t pds_eot_offset_in_bytes)
{
   struct pvr_pds_event_program pixel_event_program = { 0 };

   pvr_pds_generate_pixel_event(&pixel_event_program,
                                (uint32_t *)&pds_ptr[pds_eot_offset_in_bytes],
                                PDS_GENERATE_CODE_SEGMENT,
                                NULL);
}

VkResult
pvr_winsys_helper_fill_static_memory(struct pvr_winsys *const ws,
                                     struct pvr_winsys_vma *const general_vma,
                                     struct pvr_winsys_vma *const pds_vma,
                                     struct pvr_winsys_vma *const usc_vma)
{
   uint8_t *general_ptr, *pds_ptr, *usc_ptr;
   VkResult result;

   general_ptr = ws->ops->buffer_map(general_vma->bo);
   if (!general_ptr)
      return VK_ERROR_MEMORY_MAP_FAILED;

   pds_ptr = ws->ops->buffer_map(pds_vma->bo);
   if (!pds_ptr) {
      result = VK_ERROR_MEMORY_MAP_FAILED;
      goto err_pvr_srv_winsys_buffer_unmap_general;
   }

   usc_ptr = ws->ops->buffer_map(usc_vma->bo);
   if (!usc_ptr) {
      result = VK_ERROR_MEMORY_MAP_FAILED;
      goto err_pvr_srv_winsys_buffer_unmap_pds;
   }

   pvr_setup_static_vdm_sync(pds_ptr,
                             pds_vma->heap->static_data_offsets.vdm_sync,
                             usc_ptr,
                             usc_vma->heap->static_data_offsets.vdm_sync);

   pvr_setup_static_pixel_event_program(pds_ptr,
                                        pds_vma->heap->static_data_offsets.eot);

   ws->ops->buffer_unmap(usc_vma->bo);
   ws->ops->buffer_unmap(pds_vma->bo);
   ws->ops->buffer_unmap(general_vma->bo);

   return VK_SUCCESS;

err_pvr_srv_winsys_buffer_unmap_pds:
   ws->ops->buffer_unmap(pds_vma->bo);

err_pvr_srv_winsys_buffer_unmap_general:
   ws->ops->buffer_unmap(general_vma->bo);

   return result;
}
