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

#ifndef PVR_WINSYS_HELPER_H
#define PVR_WINSYS_HELPER_H

#include <stdbool.h>
#include <stdint.h>

#include "pvr_types.h"

struct pvr_winsys;
struct pvr_winsys_heap;
struct pvr_winsys_static_data_offsets;
struct pvr_winsys_vma;

typedef struct pvr_winsys_vma *(*const heap_alloc_reserved_func)(
   struct pvr_winsys_heap *const heap,
   const pvr_dev_addr_t reserved_dev_addr,
   uint64_t size,
   uint64_t alignment);

int pvr_winsys_helper_display_buffer_create(int master_fd,
                                            uint64_t size,
                                            uint32_t *const handle_out);
int pvr_winsys_helper_display_buffer_destroy(int master_fd, uint32_t handle);

VkResult pvr_winsys_helper_winsys_heap_init(
   struct pvr_winsys *const ws,
   pvr_dev_addr_t base_address,
   uint64_t size,
   pvr_dev_addr_t reserved_address,
   uint64_t reserved_size,
   uint32_t log2_page_size,
   const struct pvr_winsys_static_data_offsets *const static_data_offsets,
   struct pvr_winsys_heap *const heap);
bool pvr_winsys_helper_winsys_heap_finish(struct pvr_winsys_heap *const heap);

bool pvr_winsys_helper_heap_alloc(struct pvr_winsys_heap *const heap,
                                  uint64_t size,
                                  uint64_t alignment,
                                  struct pvr_winsys_vma *const vma);
void pvr_winsys_helper_heap_free(struct pvr_winsys_vma *const vma);

VkResult pvr_winsys_helper_allocate_static_memory(
   struct pvr_winsys *const ws,
   heap_alloc_reserved_func heap_alloc_reserved,
   struct pvr_winsys_heap *const general_heap,
   struct pvr_winsys_heap *const pds_heap,
   struct pvr_winsys_heap *const usc_heap,
   struct pvr_winsys_vma **const general_vma_out,
   struct pvr_winsys_vma **const pds_vma_out,
   struct pvr_winsys_vma **const usc_vma_out);
void pvr_winsys_helper_free_static_memory(
   struct pvr_winsys_vma *const general_vma,
   struct pvr_winsys_vma *const pds_vma,
   struct pvr_winsys_vma *const usc_vma);

VkResult
pvr_winsys_helper_fill_static_memory(struct pvr_winsys *const ws,
                                     struct pvr_winsys_vma *const general_vma,
                                     struct pvr_winsys_vma *const pds_vma,
                                     struct pvr_winsys_vma *const usc_vma);

#endif /* PVR_WINSYS_HELPER_H */
