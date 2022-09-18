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

#ifndef PVR_BO_H
#define PVR_BO_H

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "util/list.h"
#include "util/macros.h"

struct pvr_device;
struct pvr_winsys_bo;
struct pvr_winsys_vma;
struct pvr_winsys_heap;

struct pvr_bo {
   /* Since multiple components (csb, caching logic, etc) can make use of
    * linking buffers in a list, we add 'link' in pvr_bo to avoid an extra
    * level of structure inheritance. It's the responsibility of the buffer
    * user to manage the list and remove the buffer from the list before
    * freeing it.
    */
   struct list_head link;

   struct pvr_winsys_bo *bo;
   struct pvr_winsys_vma *vma;
};

/**
 * \brief Flag passed to #pvr_bo_alloc() to indicate that the buffer should be
 * CPU accessible. This is required in order to map a buffer with
 * #pvr_bo_cpu_map().
 */
#define PVR_BO_ALLOC_FLAG_CPU_ACCESS BITFIELD_BIT(0U)
/**
 * \brief Flag passed to #pvr_bo_alloc() to indicate that the buffer should
 * be mapped to the CPU. Implies #PVR_BO_ALLOC_FLAG_CPU_ACCESS.
 */
#define PVR_BO_ALLOC_FLAG_CPU_MAPPED BITFIELD_BIT(1U)
/**
 * \brief Flag passed to #pvr_bo_alloc() to indicate that the buffer should be
 * mapped to the GPU as uncached.
 */
#define PVR_BO_ALLOC_FLAG_GPU_UNCACHED BITFIELD_BIT(2U)
/**
 * \brief Flag passed to #pvr_bo_alloc() to indicate that the buffer GPU mapping
 * should be restricted to only allow access to the Parameter Manager unit and
 * firmware processor.
 */
#define PVR_BO_ALLOC_FLAG_PM_FW_PROTECT BITFIELD_BIT(3U)
/**
 * \brief Flag passed to #pvr_bo_alloc() to indicate that the buffer should be
 * zeroed at allocation time.
 */
#define PVR_BO_ALLOC_FLAG_ZERO_ON_ALLOC BITFIELD_BIT(4U)

VkResult pvr_bo_alloc(struct pvr_device *device,
                      struct pvr_winsys_heap *heap,
                      uint64_t size,
                      uint64_t alignment,
                      uint64_t flags,
                      struct pvr_bo **const bo_out);
void *pvr_bo_cpu_map(struct pvr_device *device, struct pvr_bo *bo);
void pvr_bo_cpu_unmap(struct pvr_device *device, struct pvr_bo *bo);
void pvr_bo_free(struct pvr_device *device, struct pvr_bo *bo);

#endif /* PVR_BO_H */
