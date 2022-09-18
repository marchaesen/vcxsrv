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
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_bo.h"
#include "pvr_private.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "vk_alloc.h"
#include "vk_log.h"

static uint32_t pvr_bo_alloc_to_winsys_flags(uint64_t flags)
{
   uint32_t ws_flags = 0;

   if (flags & (PVR_BO_ALLOC_FLAG_CPU_ACCESS | PVR_BO_ALLOC_FLAG_CPU_MAPPED))
      ws_flags |= PVR_WINSYS_BO_FLAG_CPU_ACCESS;

   if (flags & PVR_BO_ALLOC_FLAG_GPU_UNCACHED)
      ws_flags |= PVR_WINSYS_BO_FLAG_GPU_UNCACHED;

   if (flags & PVR_BO_ALLOC_FLAG_PM_FW_PROTECT)
      ws_flags |= PVR_WINSYS_BO_FLAG_PM_FW_PROTECT;

   if (flags & PVR_BO_ALLOC_FLAG_ZERO_ON_ALLOC)
      ws_flags |= PVR_WINSYS_BO_FLAG_ZERO_ON_ALLOC;

   return ws_flags;
}

/**
 * \brief Helper interface to allocate a GPU buffer and map it to both host and
 * device virtual memory. Host mapping is conditional and is controlled by
 * flags.
 *
 * \param[in] device      Logical device pointer.
 * \param[in] heap        Heap to allocate device virtual address from.
 * \param[in] size        Size of buffer to allocate.
 * \param[in] alignment   Required alignment of the allocation. Must be a power
 *                        of two.
 * \param[in] flags       Controls allocation, CPU and GPU mapping behavior
 *                        using PVR_BO_ALLOC_FLAG_*.
 * \param[out] pvr_bo_out On success output buffer is returned in this pointer.
 * \return VK_SUCCESS on success, or error code otherwise.
 *
 * \sa #pvr_bo_free()
 */
VkResult pvr_bo_alloc(struct pvr_device *device,
                      struct pvr_winsys_heap *heap,
                      uint64_t size,
                      uint64_t alignment,
                      uint64_t flags,
                      struct pvr_bo **const pvr_bo_out)
{
   const uint32_t ws_flags = pvr_bo_alloc_to_winsys_flags(flags);
   struct pvr_bo *pvr_bo;
   pvr_dev_addr_t addr;
   VkResult result;

   pvr_bo = vk_alloc(&device->vk.alloc,
                     sizeof(*pvr_bo),
                     8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pvr_bo)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = device->ws->ops->buffer_create(device->ws,
                                           size,
                                           alignment,
                                           PVR_WINSYS_BO_TYPE_GPU,
                                           ws_flags,
                                           &pvr_bo->bo);
   if (result != VK_SUCCESS)
      goto err_vk_free;

   if (flags & PVR_BO_ALLOC_FLAG_CPU_MAPPED) {
      void *map = device->ws->ops->buffer_map(pvr_bo->bo);
      if (!map) {
         result = VK_ERROR_MEMORY_MAP_FAILED;
         goto err_buffer_destroy;
      }
   }

   pvr_bo->vma = device->ws->ops->heap_alloc(heap, size, alignment);
   if (!pvr_bo->vma) {
      result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      goto err_buffer_unmap;
   }

   addr = device->ws->ops->vma_map(pvr_bo->vma, pvr_bo->bo, 0, size);
   if (!addr.addr) {
      result = VK_ERROR_MEMORY_MAP_FAILED;
      goto err_heap_free;
   }

   *pvr_bo_out = pvr_bo;

   return VK_SUCCESS;

err_heap_free:
   device->ws->ops->heap_free(pvr_bo->vma);

err_buffer_unmap:
   if (flags & PVR_BO_ALLOC_FLAG_CPU_MAPPED)
      device->ws->ops->buffer_unmap(pvr_bo->bo);

err_buffer_destroy:
   device->ws->ops->buffer_destroy(pvr_bo->bo);

err_vk_free:
   vk_free(&device->vk.alloc, pvr_bo);

   return result;
}

/**
 * \brief Interface to map the buffer into host virtual address space.
 *
 * Buffer should have been created with the #PVR_BO_ALLOC_FLAG_CPU_ACCESS
 * flag. It should also not already be mapped or it should have been unmapped
 * using #pvr_bo_cpu_unmap() before mapping again.
 *
 * \param[in] device Logical device pointer.
 * \param[in] pvr_bo Buffer to map.
 * \return Valid host virtual address on success, or NULL otherwise.
 *
 * \sa #pvr_bo_alloc(), #PVR_BO_ALLOC_FLAG_CPU_MAPPED
 */
void *pvr_bo_cpu_map(struct pvr_device *device, struct pvr_bo *pvr_bo)
{
   assert(!pvr_bo->bo->map);

   return device->ws->ops->buffer_map(pvr_bo->bo);
}

/**
 * \brief Interface to unmap the buffer from host virtual address space.
 *
 * Buffer should have a valid mapping, created either using #pvr_bo_cpu_map() or
 * by passing #PVR_BO_ALLOC_FLAG_CPU_MAPPED flag to #pvr_bo_alloc() at
 * allocation time.
 *
 * Buffer can be remapped using #pvr_bo_cpu_map().
 *
 * \param[in] device Logical device pointer.
 * \param[in] pvr_bo Buffer to unmap.
 */
void pvr_bo_cpu_unmap(struct pvr_device *device, struct pvr_bo *pvr_bo)
{
   assert(pvr_bo->bo->map);
   device->ws->ops->buffer_unmap(pvr_bo->bo);
}

/**
 * \brief Interface to free the buffer object.
 *
 * \param[in] device Logical device pointer.
 * \param[in] pvr_bo Buffer to free.
 *
 * \sa #pvr_bo_alloc()
 */
void pvr_bo_free(struct pvr_device *device, struct pvr_bo *pvr_bo)
{
   if (!pvr_bo)
      return;

   device->ws->ops->vma_unmap(pvr_bo->vma);
   device->ws->ops->heap_free(pvr_bo->vma);

   if (pvr_bo->bo->map)
      device->ws->ops->buffer_unmap(pvr_bo->bo);

   device->ws->ops->buffer_destroy(pvr_bo->bo);

   vk_free(&device->vk.alloc, pvr_bo);
}
