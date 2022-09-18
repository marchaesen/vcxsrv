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

#include "hwdef/rogue_hw_defs.h"
#include "hwdef/rogue_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_csb_enum_helpers.h"
#include "pvr_debug.h"
#include "pvr_job_common.h"
#include "pvr_job_context.h"
#include "pvr_job_render.h"
#include "pvr_pds.h"
#include "pvr_private.h"
#include "pvr_rogue_fw.h"
#include "pvr_types.h"
#include "pvr_winsys.h"
#include "util/compiler.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vk_alloc.h"
#include "vk_log.h"
#include "vk_util.h"

#define ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE 16U

/* FIXME: Is there a hardware define we can use instead? */
/* 1 DWord per PM physical page stored in the free list */
#define ROGUE_FREE_LIST_ENTRY_SIZE ((uint32_t)sizeof(uint32_t))

/* FIXME: The three defines below, for the number of PC, PD and PT entries in a
 * 4KB page, come from rgxmmudefs_km.h (meaning they're part of the
 * auto-generated hwdefs). Should these be defined in rogue_mmu.xml? Keeping in
 * mind that we probably only need these three values. */
#define ROGUE_NUM_PC_ENTRIES_PER_PAGE 0x400U

#define ROGUE_NUM_PD_ENTRIES_PER_PAGE 0x200U

#define ROGUE_NUM_PT_ENTRIES_PER_PAGE 0x200U

struct pvr_free_list {
   struct pvr_device *device;

   uint64_t size;

   struct pvr_bo *bo;

   struct pvr_winsys_free_list *ws_free_list;
};

/* Macrotile information. */
struct pvr_rt_mtile_info {
   uint32_t tile_size_x;
   uint32_t tile_size_y;

   uint32_t num_tiles_x;
   uint32_t num_tiles_y;

   uint32_t tiles_per_mtile_x;
   uint32_t tiles_per_mtile_y;

   uint32_t x_tile_max;
   uint32_t y_tile_max;

   uint32_t mtiles_x;
   uint32_t mtiles_y;

   uint32_t mtile_x1;
   uint32_t mtile_y1;
   uint32_t mtile_x2;
   uint32_t mtile_y2;
   uint32_t mtile_x3;
   uint32_t mtile_y3;

   uint32_t mtile_stride;
};

struct pvr_rt_dataset {
   struct pvr_device *device;

   /* RT dataset information */
   uint32_t width;
   uint32_t height;
   uint32_t samples;
   uint32_t layers;

   struct pvr_free_list *global_free_list;
   struct pvr_free_list *local_free_list;

   struct pvr_bo *vheap_rtc_bo;
   pvr_dev_addr_t vheap_dev_addr;
   pvr_dev_addr_t rtc_dev_addr;

   struct pvr_bo *tpc_bo;
   uint64_t tpc_stride;
   uint64_t tpc_size;

   struct pvr_winsys_rt_dataset *ws_rt_dataset;

   /* RT data information */
   struct pvr_bo *mta_mlist_bo;

   struct pvr_bo *rgn_headers_bo;
   uint64_t rgn_headers_stride;

   bool need_frag;

   uint8_t rt_data_idx;

   struct {
      pvr_dev_addr_t mta_dev_addr;
      pvr_dev_addr_t mlist_dev_addr;
      pvr_dev_addr_t rgn_headers_dev_addr;
   } rt_datas[ROGUE_NUM_RTDATAS];
};

VkResult pvr_free_list_create(struct pvr_device *device,
                              uint32_t initial_size,
                              uint32_t max_size,
                              uint32_t grow_size,
                              uint32_t grow_threshold,
                              struct pvr_free_list *parent_free_list,
                              struct pvr_free_list **const free_list_out)
{
   struct pvr_winsys_free_list *parent_ws_free_list =
      parent_free_list ? parent_free_list->ws_free_list : NULL;
   const uint64_t bo_flags = PVR_BO_ALLOC_FLAG_GPU_UNCACHED |
                             PVR_BO_ALLOC_FLAG_PM_FW_PROTECT;
   struct pvr_free_list *free_list;
   uint32_t cache_line_size;
   uint32_t initial_num_pages;
   uint32_t grow_num_pages;
   uint32_t max_num_pages;
   uint64_t addr_alignment;
   uint64_t size_alignment;
   uint64_t size;
   VkResult result;

   assert((initial_size + grow_size) <= max_size);
   assert(max_size != 0);
   assert(grow_threshold <= 100);

   /* Make sure the free list is created with at least a single page. */
   if (initial_size == 0)
      initial_size = ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE;

   /* The freelists sizes must respect the PM freelist base address alignment
    * requirement. As the freelist entries are cached by the SLC, it's also
    * necessary to ensure the sizes respect the SLC cache line size to avoid
    * invalid entries appearing in the cache, which would be problematic after
    * a grow operation, as the SLC entries aren't invalidated. We do this by
    * making sure the freelist values are appropriately aligned.
    *
    * To calculate the alignment, we first take the largest of the freelist
    * base address alignment and the SLC cache line size. We then divide this
    * by the freelist entry size to determine the number of freelist entries
    * required by the PM. Finally, as each entry holds a single PM physical
    * page, we multiple the number of entries by the page size.
    *
    * As an example, if the base address alignment is 16 bytes, the SLC cache
    * line size is 64 bytes and the freelist entry size is 4 bytes then 16
    * entries are required, as we take the SLC cacheline size (being the larger
    * of the two values) and divide this by 4. If the PM page size is 4096
    * bytes then we end up with an alignment of 65536 bytes.
    */
   cache_line_size = rogue_get_slc_cache_line_size(&device->pdevice->dev_info);

   addr_alignment =
      MAX2(ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE, cache_line_size);
   size_alignment = (addr_alignment / ROGUE_FREE_LIST_ENTRY_SIZE) *
                    ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE;

   assert(util_is_power_of_two_nonzero(size_alignment));

   initial_size = align64(initial_size, size_alignment);
   max_size = align64(max_size, size_alignment);
   grow_size = align64(grow_size, size_alignment);

   /* Make sure the 'max' size doesn't exceed what the firmware supports and
    * adjust the other sizes accordingly.
    */
   if (max_size > ROGUE_FREE_LIST_MAX_SIZE) {
      max_size = ROGUE_FREE_LIST_MAX_SIZE;
      assert(align64(max_size, size_alignment) == max_size);
   }

   if (initial_size > max_size)
      initial_size = max_size;

   if (initial_size == max_size)
      grow_size = 0;

   initial_num_pages = initial_size >> ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;
   max_num_pages = max_size >> ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;
   grow_num_pages = grow_size >> ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;

   /* Calculate the size of the buffer needed to store the free list entries
    * based on the maximum number of pages we can have.
    */
   size = max_num_pages * ROGUE_FREE_LIST_ENTRY_SIZE;
   assert(align64(size, addr_alignment) == size);

   free_list = vk_alloc(&device->vk.alloc,
                        sizeof(*free_list),
                        8,
                        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!free_list)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: The memory is mapped GPU uncached, but this seems to contradict
    * the comment above about aligning to the SLC cache line size.
    */
   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         size,
                         addr_alignment,
                         bo_flags,
                         &free_list->bo);
   if (result != VK_SUCCESS)
      goto err_vk_free_free_list;

   result = device->ws->ops->free_list_create(device->ws,
                                              free_list->bo->vma,
                                              initial_num_pages,
                                              max_num_pages,
                                              grow_num_pages,
                                              grow_threshold,
                                              parent_ws_free_list,
                                              &free_list->ws_free_list);
   if (result != VK_SUCCESS)
      goto err_pvr_bo_free_bo;

   free_list->device = device;
   free_list->size = size;

   *free_list_out = free_list;

   return VK_SUCCESS;

err_pvr_bo_free_bo:
   pvr_bo_free(device, free_list->bo);

err_vk_free_free_list:
   vk_free(&device->vk.alloc, free_list);

   return result;
}

void pvr_free_list_destroy(struct pvr_free_list *free_list)
{
   struct pvr_device *device = free_list->device;

   device->ws->ops->free_list_destroy(free_list->ws_free_list);
   pvr_bo_free(device, free_list->bo);
   vk_free(&device->vk.alloc, free_list);
}

static inline void pvr_get_samples_in_xy(uint32_t samples,
                                         uint32_t *const x_out,
                                         uint32_t *const y_out)
{
   switch (samples) {
   case 1:
      *x_out = 1;
      *y_out = 1;
      break;
   case 2:
      *x_out = 1;
      *y_out = 2;
      break;
   case 4:
      *x_out = 2;
      *y_out = 2;
      break;
   case 8:
      *x_out = 2;
      *y_out = 4;
      break;
   default:
      unreachable("Unsupported number of samples");
   }
}

static void pvr_rt_mtile_info_init(struct pvr_device *device,
                                   struct pvr_rt_mtile_info *info,
                                   uint32_t width,
                                   uint32_t height,
                                   uint32_t samples)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   uint32_t samples_in_x;
   uint32_t samples_in_y;

   pvr_get_samples_in_xy(samples, &samples_in_x, &samples_in_y);

   info->tile_size_x = PVR_GET_FEATURE_VALUE(dev_info, tile_size_x, 1);
   info->tile_size_y = PVR_GET_FEATURE_VALUE(dev_info, tile_size_y, 1);

   info->num_tiles_x = DIV_ROUND_UP(width, info->tile_size_x);
   info->num_tiles_y = DIV_ROUND_UP(height, info->tile_size_y);

   rogue_get_num_macrotiles_xy(dev_info, &info->mtiles_x, &info->mtiles_y);

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
      assert(PVR_GET_FEATURE_VALUE(dev_info,
                                   simple_parameter_format_version,
                                   0) == 2);
      /* Set up 16 macrotiles with a multiple of 2x2 tiles per macrotile,
       * which is aligned to a tile group.
       */
      info->mtile_x1 = DIV_ROUND_UP(info->num_tiles_x, 8) * 2;
      info->mtile_y1 = DIV_ROUND_UP(info->num_tiles_y, 8) * 2;
      info->mtile_x2 = 0;
      info->mtile_y2 = 0;
      info->mtile_x3 = 0;
      info->mtile_y3 = 0;
      info->x_tile_max = ALIGN_POT(info->num_tiles_x, 2) - 1;
      info->y_tile_max = ALIGN_POT(info->num_tiles_y, 2) - 1;
   } else {
      /* Set up 16 macrotiles with a multiple of 4x4 tiles per macrotile. */
      info->mtile_x1 = ALIGN_POT(DIV_ROUND_UP(info->num_tiles_x, 4), 4);
      info->mtile_y1 = ALIGN_POT(DIV_ROUND_UP(info->num_tiles_y, 4), 4);
      info->mtile_x2 = info->mtile_x1 * 2;
      info->mtile_y2 = info->mtile_y1 * 2;
      info->mtile_x3 = info->mtile_x1 * 3;
      info->mtile_y3 = info->mtile_y1 * 3;
      info->x_tile_max = info->num_tiles_x - 1;
      info->y_tile_max = info->num_tiles_y - 1;
   }

   info->tiles_per_mtile_x = info->mtile_x1 * samples_in_x;
   info->tiles_per_mtile_y = info->mtile_y1 * samples_in_y;

   info->mtile_stride = info->mtile_x1 * info->mtile_y1;
}

/* Note that the unit of the return value depends on the GPU. For cores with the
 * simple_internal_parameter_format feature the returned size is interpreted as
 * the number of region headers. For cores without this feature its interpreted
 * as the size in dwords.
 */
static uint64_t
pvr_rt_get_isp_region_size(struct pvr_device *device,
                           const struct pvr_rt_mtile_info *mtile_info)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   uint64_t rgn_size =
      mtile_info->tiles_per_mtile_x * mtile_info->tiles_per_mtile_y;

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
      uint32_t version;

      rgn_size *= mtile_info->mtiles_x * mtile_info->mtiles_y;

      if (PVR_FEATURE_VALUE(dev_info,
                            simple_parameter_format_version,
                            &version)) {
         version = 0;
      }

      if (version == 2) {
         /* One region header per 2x2 tile group. */
         rgn_size /= (2U * 2U);
      }
   } else {
      const uint64_t rgn_header_size = rogue_get_region_header_size(dev_info);

      /* Round up to next dword to prevent IPF overrun and convert to bytes.
       */
      rgn_size = DIV_ROUND_UP(rgn_size * rgn_header_size, 4);
   }

   return rgn_size;
}

static VkResult pvr_rt_vheap_rtc_data_init(struct pvr_device *device,
                                           struct pvr_rt_dataset *rt_dataset,
                                           uint32_t layers)
{
   const uint64_t bo_flags = PVR_BO_ALLOC_FLAG_GPU_UNCACHED |
                             PVR_BO_ALLOC_FLAG_ZERO_ON_ALLOC;
   uint64_t vheap_size;
   uint32_t alignment;
   uint64_t rtc_size;
   VkResult result;

   vheap_size = ROGUE_CR_PM_VHEAP_TABLE_SIZE * ROGUE_PM_VHEAP_ENTRY_SIZE;

   if (layers > 1) {
      uint64_t rtc_entries;

      vheap_size = ALIGN_POT(vheap_size, PVRX(CR_TA_RTC_ADDR_BASE_ALIGNMENT));

      rtc_entries = ROGUE_NUM_TEAC + ROGUE_NUM_TE + ROGUE_NUM_VCE;
      if (PVR_HAS_QUIRK(&device->pdevice->dev_info, 48545))
         rtc_entries += ROGUE_NUM_TE;

      rtc_size = rtc_entries * ROGUE_RTC_SIZE_IN_BYTES;
   } else {
      rtc_size = 0;
   }

   alignment = MAX2(PVRX(CR_PM_VHEAP_TABLE_BASE_ADDR_ALIGNMENT),
                    PVRX(CR_TA_RTC_ADDR_BASE_ALIGNMENT));

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         vheap_size + rtc_size,
                         alignment,
                         bo_flags,
                         &rt_dataset->vheap_rtc_bo);
   if (result != VK_SUCCESS)
      return result;

   rt_dataset->vheap_dev_addr = rt_dataset->vheap_rtc_bo->vma->dev_addr;

   if (rtc_size > 0) {
      rt_dataset->rtc_dev_addr =
         PVR_DEV_ADDR_OFFSET(rt_dataset->vheap_dev_addr, vheap_size);
   } else {
      rt_dataset->rtc_dev_addr = PVR_DEV_ADDR_INVALID;
   }

   return VK_SUCCESS;
}

static void pvr_rt_vheap_rtc_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   rt_dataset->rtc_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->vheap_rtc_bo);
   rt_dataset->vheap_rtc_bo = NULL;
}

static void
pvr_rt_get_tail_ptr_stride_size(const struct pvr_device *device,
                                const struct pvr_rt_mtile_info *mtile_info,
                                uint32_t layers,
                                uint64_t *const stride_out,
                                uint64_t *const size_out)
{
   uint32_t max_num_mtiles;
   uint32_t num_mtiles_x;
   uint32_t num_mtiles_y;
   uint32_t version;
   uint64_t size;

   num_mtiles_x = mtile_info->mtiles_x * mtile_info->tiles_per_mtile_x;
   num_mtiles_y = mtile_info->mtiles_y * mtile_info->tiles_per_mtile_y;

   max_num_mtiles = MAX2(util_next_power_of_two64(num_mtiles_x),
                         util_next_power_of_two64(num_mtiles_y));

   size = max_num_mtiles * max_num_mtiles;

   if (PVR_FEATURE_VALUE(&device->pdevice->dev_info,
                         simple_parameter_format_version,
                         &version)) {
      version = 0;
   }

   if (version == 2) {
      /* One tail pointer cache entry per 2x2 tile group. */
      size /= (2U * 2U);
   }

   size *= ROGUE_TAIL_POINTER_SIZE;

   if (layers > 1) {
      size = ALIGN_POT(size, ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE);

      *stride_out = size / ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE;
      *size_out = size * layers;
   } else {
      *stride_out = 0;
      *size_out = size;
   }
}

static VkResult pvr_rt_tpc_data_init(struct pvr_device *device,
                                     struct pvr_rt_dataset *rt_dataset,
                                     const struct pvr_rt_mtile_info *mtile_info,
                                     uint32_t layers)
{
   const uint64_t bo_flags = PVR_BO_ALLOC_FLAG_GPU_UNCACHED |
                             PVR_BO_ALLOC_FLAG_ZERO_ON_ALLOC;
   uint64_t tpc_size;

   pvr_rt_get_tail_ptr_stride_size(device,
                                   mtile_info,
                                   layers,
                                   &rt_dataset->tpc_stride,
                                   &rt_dataset->tpc_size);
   tpc_size = ALIGN_POT(rt_dataset->tpc_size, ROGUE_TE_TPC_CACHE_LINE_SIZE);

   return pvr_bo_alloc(device,
                       device->heaps.general_heap,
                       tpc_size,
                       PVRX(CR_TE_TPC_ADDR_BASE_ALIGNMENT),
                       bo_flags,
                       &rt_dataset->tpc_bo);
}

static void pvr_rt_tpc_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   pvr_bo_free(rt_dataset->device, rt_dataset->tpc_bo);
   rt_dataset->tpc_bo = NULL;
}

static uint32_t
pvr_rt_get_mlist_size(const struct pvr_free_list *global_free_list,
                      const struct pvr_free_list *local_free_list)
{
   uint32_t num_pte_pages;
   uint32_t num_pde_pages;
   uint32_t num_pce_pages;
   uint64_t total_pages;
   uint32_t mlist_size;

   assert(global_free_list->size + local_free_list->size <=
          ROGUE_PM_MAX_PB_VIRT_ADDR_SPACE);

   total_pages = (global_free_list->size + local_free_list->size) >>
                 ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;

   /* Calculate the total number of physical pages required to hold the page
    * table, directory and catalog entries for the freelist pages.
    */
   num_pte_pages = DIV_ROUND_UP(total_pages, ROGUE_NUM_PT_ENTRIES_PER_PAGE);
   num_pde_pages = DIV_ROUND_UP(num_pte_pages, ROGUE_NUM_PD_ENTRIES_PER_PAGE);
   num_pce_pages = DIV_ROUND_UP(num_pde_pages, ROGUE_NUM_PC_ENTRIES_PER_PAGE);

   /* Calculate the MList size considering the total number of pages in the PB
    * are shared among all the PM address spaces.
    */
   mlist_size = (num_pce_pages + num_pde_pages + num_pte_pages) *
                ROGUE_NUM_PM_ADDRESS_SPACES * ROGUE_MLIST_ENTRY_STRIDE;

   return ALIGN_POT(mlist_size, ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE);
}

static void pvr_rt_get_region_headers_stride_size(
   const struct pvr_device *device,
   const struct pvr_rt_mtile_info *mtile_info,
   uint32_t layers,
   uint64_t *const stride_out,
   uint64_t *const size_out)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   const uint32_t rgn_header_size = rogue_get_region_header_size(dev_info);
   uint32_t rgn_headers_size;
   uint32_t num_tiles_x;
   uint32_t num_tiles_y;
   uint32_t group_size;
   uint32_t version;

   if (PVR_FEATURE_VALUE(dev_info, simple_parameter_format_version, &version))
      version = 0;

   group_size = version == 2 ? 2 : 1;

   num_tiles_x = mtile_info->mtiles_x * mtile_info->tiles_per_mtile_x;
   num_tiles_y = mtile_info->mtiles_y * mtile_info->tiles_per_mtile_y;

   rgn_headers_size =
      (num_tiles_x / group_size) * (num_tiles_y / group_size) * rgn_header_size;

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
      rgn_headers_size =
         ALIGN_POT(rgn_headers_size, PVRX(CR_TE_PSGREGION_ADDR_BASE_ALIGNMENT));
   }

   if (layers > 1) {
      rgn_headers_size =
         ALIGN_POT(rgn_headers_size, PVRX(CR_TE_PSG_REGION_STRIDE_UNIT_SIZE));
   }

   *stride_out = rgn_header_size;
   *size_out = rgn_headers_size * layers;
}

static VkResult
pvr_rt_mta_mlist_data_init(struct pvr_device *device,
                           struct pvr_rt_dataset *rt_dataset,
                           const struct pvr_free_list *global_free_list,
                           const struct pvr_free_list *local_free_list,
                           const struct pvr_rt_mtile_info *mtile_info)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   const uint32_t mlist_size =
      pvr_rt_get_mlist_size(global_free_list, local_free_list);
   uint32_t mta_size = rogue_get_macrotile_array_size(dev_info);
   const uint32_t num_rt_datas = ARRAY_SIZE(rt_dataset->rt_datas);
   uint32_t rt_datas_mlist_size;
   uint32_t rt_datas_mta_size;
   pvr_dev_addr_t dev_addr;
   VkResult result;

   /* Allocate memory for macrotile array and Mlist for all RT datas.
    *
    * Allocation layout: MTA[0..N] + Mlist alignment padding + Mlist[0..N].
    *
    * N is number of RT datas.
    */
   rt_datas_mta_size = ALIGN_POT(mta_size * num_rt_datas,
                                 PVRX(CR_PM_MLIST0_BASE_ADDR_ALIGNMENT));
   rt_datas_mlist_size = mlist_size * num_rt_datas;

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         rt_datas_mta_size + rt_datas_mlist_size,
                         PVRX(CR_PM_MTILE_ARRAY_BASE_ADDR_ALIGNMENT),
                         PVR_BO_ALLOC_FLAG_GPU_UNCACHED,
                         &rt_dataset->mta_mlist_bo);
   if (result != VK_SUCCESS)
      return result;

   dev_addr = rt_dataset->mta_mlist_bo->vma->dev_addr;

   for (uint32_t i = 0; i < num_rt_datas; i++) {
      if (mta_size != 0) {
         rt_dataset->rt_datas[i].mta_dev_addr = dev_addr;
         dev_addr = PVR_DEV_ADDR_OFFSET(dev_addr, mta_size);
      } else {
         rt_dataset->rt_datas[i].mta_dev_addr = PVR_DEV_ADDR_INVALID;
      }
   }

   dev_addr = PVR_DEV_ADDR_OFFSET(rt_dataset->mta_mlist_bo->vma->dev_addr,
                                  rt_datas_mta_size);

   for (uint32_t i = 0; i < num_rt_datas; i++) {
      if (mlist_size != 0) {
         rt_dataset->rt_datas[i].mlist_dev_addr = dev_addr;
         dev_addr = PVR_DEV_ADDR_OFFSET(dev_addr, mlist_size);
      } else {
         rt_dataset->rt_datas[i].mlist_dev_addr = PVR_DEV_ADDR_INVALID;
      }
   }

   return VK_SUCCESS;
}

static void pvr_rt_mta_mlist_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++) {
      rt_dataset->rt_datas[i].mlist_dev_addr = PVR_DEV_ADDR_INVALID;
      rt_dataset->rt_datas[i].mta_dev_addr = PVR_DEV_ADDR_INVALID;
   }

   pvr_bo_free(rt_dataset->device, rt_dataset->mta_mlist_bo);
   rt_dataset->mta_mlist_bo = NULL;
}

static VkResult
pvr_rt_rgn_headers_data_init(struct pvr_device *device,
                             struct pvr_rt_dataset *rt_dataset,
                             const struct pvr_rt_mtile_info *mtile_info,
                             uint32_t layers)
{
   const uint32_t num_rt_datas = ARRAY_SIZE(rt_dataset->rt_datas);
   uint64_t rgn_headers_size;
   pvr_dev_addr_t dev_addr;
   VkResult result;

   pvr_rt_get_region_headers_stride_size(device,
                                         mtile_info,
                                         layers,
                                         &rt_dataset->rgn_headers_stride,
                                         &rgn_headers_size);

   result = pvr_bo_alloc(device,
                         device->heaps.rgn_hdr_heap,
                         rgn_headers_size * num_rt_datas,
                         PVRX(CR_TE_PSGREGION_ADDR_BASE_ALIGNMENT),
                         PVR_BO_ALLOC_FLAG_GPU_UNCACHED,
                         &rt_dataset->rgn_headers_bo);
   if (result != VK_SUCCESS)
      return result;

   dev_addr = rt_dataset->rgn_headers_bo->vma->dev_addr;

   for (uint32_t i = 0; i < num_rt_datas; i++) {
      rt_dataset->rt_datas[i].rgn_headers_dev_addr = dev_addr;
      dev_addr = PVR_DEV_ADDR_OFFSET(dev_addr, rgn_headers_size);
   }

   return VK_SUCCESS;
}

static void pvr_rt_rgn_headers_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].rgn_headers_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->rgn_headers_bo);
   rt_dataset->rgn_headers_bo = NULL;
}

static VkResult pvr_rt_datas_init(struct pvr_device *device,
                                  struct pvr_rt_dataset *rt_dataset,
                                  const struct pvr_free_list *global_free_list,
                                  const struct pvr_free_list *local_free_list,
                                  const struct pvr_rt_mtile_info *mtile_info,
                                  uint32_t layers)
{
   VkResult result;

   result = pvr_rt_mta_mlist_data_init(device,
                                       rt_dataset,
                                       global_free_list,
                                       local_free_list,
                                       mtile_info);
   if (result != VK_SUCCESS)
      return result;

   result =
      pvr_rt_rgn_headers_data_init(device, rt_dataset, mtile_info, layers);
   if (result != VK_SUCCESS)
      goto err_pvr_rt_mta_mlist_data_fini;

   return VK_SUCCESS;

err_pvr_rt_mta_mlist_data_fini:
   pvr_rt_mta_mlist_data_fini(rt_dataset);

   return VK_SUCCESS;
}

static void pvr_rt_datas_fini(struct pvr_rt_dataset *rt_dataset)
{
   pvr_rt_rgn_headers_data_fini(rt_dataset);
   pvr_rt_mta_mlist_data_fini(rt_dataset);
}

static uint32_t
pvr_rogue_get_cr_isp_mtile_size_val(const struct pvr_device_info *dev_info,
                                    uint32_t samples,
                                    const struct pvr_rt_mtile_info *mtile_info)
{
   uint32_t samples_per_pixel =
      PVR_GET_FEATURE_VALUE(dev_info, isp_samples_per_pixel, 0);
   uint32_t isp_mtile_size;

   pvr_csb_pack (&isp_mtile_size, CR_ISP_MTILE_SIZE, value) {
      value.x = mtile_info->mtile_x1;
      value.y = mtile_info->mtile_y1;

      if (samples_per_pixel == 1) {
         if (samples >= 4)
            value.x <<= 1;

         if (samples >= 2)
            value.y <<= 1;
      } else if (samples_per_pixel == 2) {
         if (samples >= 8)
            value.x <<= 1;

         if (samples >= 4)
            value.y <<= 1;
      } else if (samples_per_pixel == 4) {
         if (samples >= 8)
            value.y <<= 1;
      } else {
         assert(!"Unsupported ISP samples per pixel value");
      }
   }

   return isp_mtile_size;
}

static uint64_t pvr_rogue_get_cr_multisamplectl_val(uint32_t samples,
                                                    bool y_flip)
{
   static const struct {
      uint8_t x[8];
      uint8_t y[8];
   } sample_positions[4] = {
      /* 1 sample */
      {
         .x = { 8 },
         .y = { 8 },
      },
      /* 2 samples */
      {
         .x = { 12, 4 },
         .y = { 12, 4 },
      },
      /* 4 samples */
      {
         .x = { 6, 14, 2, 10 },
         .y = { 2, 6, 10, 14 },
      },
      /* 8 samples */
      {
         .x = { 9, 7, 13, 5, 3, 1, 11, 15 },
         .y = { 5, 11, 9, 3, 13, 7, 15, 1 },
      },
   };
   uint64_t multisamplectl;
   uint8_t idx;

   idx = util_fast_log2(samples);
   assert(idx < ARRAY_SIZE(sample_positions));

   pvr_csb_pack (&multisamplectl, CR_PPP_MULTISAMPLECTL, value) {
      switch (samples) {
      case 8:
         value.msaa_x7 = sample_positions[idx].x[7];
         value.msaa_x6 = sample_positions[idx].x[6];
         value.msaa_x5 = sample_positions[idx].x[5];
         value.msaa_x4 = sample_positions[idx].x[4];

         if (y_flip) {
            value.msaa_y7 = 16U - sample_positions[idx].y[7];
            value.msaa_y6 = 16U - sample_positions[idx].y[6];
            value.msaa_y5 = 16U - sample_positions[idx].y[5];
            value.msaa_y4 = 16U - sample_positions[idx].y[4];
         } else {
            value.msaa_y7 = sample_positions[idx].y[7];
            value.msaa_y6 = sample_positions[idx].y[6];
            value.msaa_y5 = sample_positions[idx].y[5];
            value.msaa_y4 = sample_positions[idx].y[4];
         }

         FALLTHROUGH;
      case 4:
         value.msaa_x3 = sample_positions[idx].x[3];
         value.msaa_x2 = sample_positions[idx].x[2];

         if (y_flip) {
            value.msaa_y3 = 16U - sample_positions[idx].y[3];
            value.msaa_y2 = 16U - sample_positions[idx].y[2];
         } else {
            value.msaa_y3 = sample_positions[idx].y[3];
            value.msaa_y2 = sample_positions[idx].y[2];
         }

         FALLTHROUGH;
      case 2:
         value.msaa_x1 = sample_positions[idx].x[1];

         if (y_flip) {
            value.msaa_y1 = 16U - sample_positions[idx].y[1];
         } else {
            value.msaa_y1 = sample_positions[idx].y[1];
         }

         FALLTHROUGH;
      case 1:
         value.msaa_x0 = sample_positions[idx].x[0];

         if (y_flip) {
            value.msaa_y0 = 16U - sample_positions[idx].y[0];
         } else {
            value.msaa_y0 = sample_positions[idx].y[0];
         }

         break;
      default:
         unreachable("Unsupported number of samples");
      }
   }

   return multisamplectl;
}

static uint32_t
pvr_rogue_get_cr_te_aa_val(const struct pvr_device_info *dev_info,
                           uint32_t samples)
{
   uint32_t samples_per_pixel =
      PVR_GET_FEATURE_VALUE(dev_info, isp_samples_per_pixel, 0);
   uint32_t te_aa;

   pvr_csb_pack (&te_aa, CR_TE_AA, value) {
      if (samples_per_pixel == 1) {
         if (samples >= 2)
            value.y = true;
         if (samples >= 4)
            value.x = true;
      } else if (samples_per_pixel == 2) {
         if (samples >= 2)
            value.x2 = true;
         if (samples >= 4)
            value.y = true;
         if (samples >= 8)
            value.x = true;
      } else if (samples_per_pixel == 4) {
         if (samples >= 2)
            value.x2 = true;
         if (samples >= 4)
            value.y2 = true;
         if (samples >= 8)
            value.y = true;
      } else {
         assert(!"Unsupported ISP samples per pixel value");
      }
   }

   return te_aa;
}

static void pvr_rt_dataset_ws_create_info_init(
   struct pvr_rt_dataset *rt_dataset,
   const struct pvr_rt_mtile_info *mtile_info,
   struct pvr_winsys_rt_dataset_create_info *create_info)
{
   struct pvr_device *device = rt_dataset->device;
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;

   memset(create_info, 0, sizeof(*create_info));

   /* Local freelist. */
   create_info->local_free_list = rt_dataset->local_free_list->ws_free_list;

   /* ISP register values. */
   if (PVR_HAS_ERN(dev_info, 42307) &&
       !(PVR_HAS_FEATURE(dev_info, roguexe) && mtile_info->tile_size_x == 16)) {
      float value;

      if (rt_dataset->width != 0) {
         value =
            ROGUE_ISP_MERGE_LOWER_LIMIT_NUMERATOR / (float)rt_dataset->width;
         create_info->isp_merge_lower_x = fui(value);

         value =
            ROGUE_ISP_MERGE_UPPER_LIMIT_NUMERATOR / (float)rt_dataset->width;
         create_info->isp_merge_upper_x = fui(value);
      }

      if (rt_dataset->height != 0) {
         value =
            ROGUE_ISP_MERGE_LOWER_LIMIT_NUMERATOR / (float)rt_dataset->height;
         create_info->isp_merge_lower_y = fui(value);

         value =
            ROGUE_ISP_MERGE_UPPER_LIMIT_NUMERATOR / (float)rt_dataset->height;
         create_info->isp_merge_upper_y = fui(value);
      }

      value = ((float)rt_dataset->width * ROGUE_ISP_MERGE_SCALE_FACTOR) /
              (ROGUE_ISP_MERGE_UPPER_LIMIT_NUMERATOR -
               ROGUE_ISP_MERGE_LOWER_LIMIT_NUMERATOR);
      create_info->isp_merge_scale_x = fui(value);

      value = ((float)rt_dataset->height * ROGUE_ISP_MERGE_SCALE_FACTOR) /
              (ROGUE_ISP_MERGE_UPPER_LIMIT_NUMERATOR -
               ROGUE_ISP_MERGE_LOWER_LIMIT_NUMERATOR);
      create_info->isp_merge_scale_y = fui(value);
   }

   create_info->isp_mtile_size =
      pvr_rogue_get_cr_isp_mtile_size_val(dev_info,
                                          rt_dataset->samples,
                                          mtile_info);

   /* PPP register values. */
   create_info->ppp_multi_sample_ctl =
      pvr_rogue_get_cr_multisamplectl_val(rt_dataset->samples, false);
   create_info->ppp_multi_sample_ctl_y_flipped =
      pvr_rogue_get_cr_multisamplectl_val(rt_dataset->samples, true);

   pvr_csb_pack (&create_info->ppp_screen, CR_PPP_SCREEN, value) {
      value.pixxmax = rt_dataset->width - 1;
      value.pixymax = rt_dataset->height - 1;
   }

   /* TE register values. */
   create_info->te_aa =
      pvr_rogue_get_cr_te_aa_val(dev_info, rt_dataset->samples);

   pvr_csb_pack (&create_info->te_mtile1, CR_TE_MTILE1, value) {
      value.x1 = mtile_info->mtile_x1;
      if (!PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
         value.x2 = mtile_info->mtile_x2;
         value.x3 = mtile_info->mtile_x3;
      }
   }

   pvr_csb_pack (&create_info->te_mtile2, CR_TE_MTILE2, value) {
      value.y1 = mtile_info->mtile_y1;
      if (!PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
         value.y2 = mtile_info->mtile_y2;
         value.y3 = mtile_info->mtile_y3;
      }
   }

   pvr_csb_pack (&create_info->te_screen, CR_TE_SCREEN, value) {
      value.xmax = mtile_info->x_tile_max;
      value.ymax = mtile_info->y_tile_max;
   }

   /* Allocations and associated information. */
   create_info->vheap_table_dev_addr = rt_dataset->vheap_dev_addr;
   create_info->rtc_dev_addr = rt_dataset->rtc_dev_addr;

   create_info->tpc_dev_addr = rt_dataset->tpc_bo->vma->dev_addr;
   create_info->tpc_stride = rt_dataset->tpc_stride;
   create_info->tpc_size = rt_dataset->tpc_size;

   STATIC_ASSERT(ARRAY_SIZE(create_info->rt_datas) ==
                 ARRAY_SIZE(rt_dataset->rt_datas));
   for (uint32_t i = 0; i < ARRAY_SIZE(create_info->rt_datas); i++) {
      create_info->rt_datas[i].pm_mlist_dev_addr =
         rt_dataset->rt_datas[i].mlist_dev_addr;
      create_info->rt_datas[i].macrotile_array_dev_addr =
         rt_dataset->rt_datas[i].mta_dev_addr;
      create_info->rt_datas[i].rgn_header_dev_addr =
         rt_dataset->rt_datas[i].rgn_headers_dev_addr;
   }

   create_info->rgn_header_size =
      pvr_rt_get_isp_region_size(device, mtile_info);

   /* Miscellaneous. */
   create_info->mtile_stride = mtile_info->mtile_stride;
   create_info->max_rts = rt_dataset->layers;
}

VkResult
pvr_render_target_dataset_create(struct pvr_device *device,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t samples,
                                 uint32_t layers,
                                 struct pvr_rt_dataset **const rt_dataset_out)
{
   struct pvr_device_runtime_info *runtime_info =
      &device->pdevice->dev_runtime_info;
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_winsys_rt_dataset_create_info rt_dataset_create_info;
   struct pvr_rt_mtile_info mtile_info;
   struct pvr_rt_dataset *rt_dataset;
   VkResult result;

   assert(device->global_free_list);
   assert(width <= rogue_get_render_size_max_x(dev_info));
   assert(height <= rogue_get_render_size_max_y(dev_info));
   assert(layers > 0 && layers <= PVR_MAX_FRAMEBUFFER_LAYERS);

   pvr_rt_mtile_info_init(device, &mtile_info, width, height, samples);

   rt_dataset = vk_zalloc(&device->vk.alloc,
                          sizeof(*rt_dataset),
                          8,
                          VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!rt_dataset)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   rt_dataset->device = device;
   rt_dataset->width = width;
   rt_dataset->height = height;
   rt_dataset->samples = samples;
   rt_dataset->layers = layers;
   rt_dataset->global_free_list = device->global_free_list;

   /* The maximum supported free list size is based on the assumption that this
    * freelist (the "local" freelist) is always the minimum size required by
    * the hardware. See the documentation of ROGUE_FREE_LIST_MAX_SIZE for more
    * details.
    */
   result = pvr_free_list_create(device,
                                 runtime_info->min_free_list_size,
                                 runtime_info->min_free_list_size,
                                 0 /* grow_size */,
                                 0 /* grow_threshold */,
                                 rt_dataset->global_free_list,
                                 &rt_dataset->local_free_list);
   if (result != VK_SUCCESS)
      goto err_vk_free_rt_dataset;

   result = pvr_rt_vheap_rtc_data_init(device, rt_dataset, layers);
   if (result != VK_SUCCESS)
      goto err_pvr_free_list_destroy;

   result = pvr_rt_tpc_data_init(device, rt_dataset, &mtile_info, layers);
   if (result != VK_SUCCESS)
      goto err_pvr_rt_vheap_rtc_data_fini;

   result = pvr_rt_datas_init(device,
                              rt_dataset,
                              rt_dataset->global_free_list,
                              rt_dataset->local_free_list,
                              &mtile_info,
                              layers);
   if (result != VK_SUCCESS)
      goto err_pvr_rt_tpc_data_fini;

   /* rt_dataset must be fully initialized by this point since
    * pvr_rt_dataset_ws_create_info_init() depends on this.
    */
   pvr_rt_dataset_ws_create_info_init(rt_dataset,
                                      &mtile_info,
                                      &rt_dataset_create_info);

   result =
      device->ws->ops->render_target_dataset_create(device->ws,
                                                    &rt_dataset_create_info,
                                                    &rt_dataset->ws_rt_dataset);
   if (result != VK_SUCCESS)
      goto err_pvr_rt_datas_fini;

   *rt_dataset_out = rt_dataset;

   return VK_SUCCESS;

err_pvr_rt_datas_fini:
   pvr_rt_datas_fini(rt_dataset);

err_pvr_rt_tpc_data_fini:
   pvr_rt_tpc_data_fini(rt_dataset);

err_pvr_rt_vheap_rtc_data_fini:
   pvr_rt_vheap_rtc_data_fini(rt_dataset);

err_pvr_free_list_destroy:
   pvr_free_list_destroy(rt_dataset->local_free_list);

err_vk_free_rt_dataset:
   vk_free(&device->vk.alloc, rt_dataset);

   return result;
}

void pvr_render_target_dataset_destroy(struct pvr_rt_dataset *rt_dataset)
{
   struct pvr_device *device = rt_dataset->device;

   device->ws->ops->render_target_dataset_destroy(rt_dataset->ws_rt_dataset);

   pvr_rt_datas_fini(rt_dataset);
   pvr_rt_tpc_data_fini(rt_dataset);
   pvr_rt_vheap_rtc_data_fini(rt_dataset);

   pvr_free_list_destroy(rt_dataset->local_free_list);

   vk_free(&device->vk.alloc, rt_dataset);
}

static void
pvr_render_job_ws_geometry_state_init(struct pvr_render_ctx *ctx,
                                      struct pvr_render_job *job,
                                      struct pvr_winsys_geometry_state *state)
{
   const struct pvr_device_info *dev_info = &ctx->device->pdevice->dev_info;

   /* FIXME: Should this just be done unconditionally? The firmware will just
    * ignore the value anyway.
    */
   if (PVR_HAS_QUIRK(dev_info, 56279)) {
      pvr_csb_pack (&state->regs.pds_ctrl, CR_PDS_CTRL, value) {
         value.max_num_vdm_tasks = rogue_get_max_num_vdm_pds_tasks(dev_info);
      }
   } else {
      state->regs.pds_ctrl = 0;
   }

   pvr_csb_pack (&state->regs.ppp_ctrl, CR_PPP_CTRL, value) {
      value.wclampen = true;
      value.fixed_point_format = 1;
   }

   pvr_csb_pack (&state->regs.te_psg, CR_TE_PSG, value) {
      value.completeonterminate = job->geometry_terminate;

      value.region_stride = job->rt_dataset->rgn_headers_stride /
                            PVRX(CR_TE_PSG_REGION_STRIDE_UNIT_SIZE);

      value.forcenewstate = PVR_HAS_QUIRK(dev_info, 52942);
   }

   /* The set up of CR_TPU must be identical to
    * pvr_render_job_ws_fragment_state_init().
    */
   pvr_csb_pack (&state->regs.tpu, CR_TPU, value) {
      value.tag_cem_4k_face_packing = true;
   }

   pvr_csb_pack (&state->regs.tpu_border_colour_table,
                 CR_TPU_BORDER_COLOUR_TABLE_VDM,
                 value) {
      value.border_colour_table_address = job->border_colour_table_addr;
   }

   pvr_csb_pack (&state->regs.vdm_ctrl_stream_base,
                 CR_VDM_CTRL_STREAM_BASE,
                 value) {
      value.addr = job->ctrl_stream_addr;
   }

   /* Set up the USC common size for the context switch resume/load program
    * (ctx->ctx_switch.programs[i].sr->pds_load_program), which was created
    * as part of the render context.
    */
   pvr_csb_pack (&state->regs.vdm_ctx_resume_task0_size,
                 VDMCTRL_PDS_STATE0,
                 value) {
      /* Calculate the size in bytes. */
      const uint16_t shared_registers_size = job->max_shared_registers * 4;

      value.usc_common_size =
         DIV_ROUND_UP(shared_registers_size,
                      PVRX(VDMCTRL_PDS_STATE0_USC_COMMON_SIZE_UNIT_SIZE));
   };

   state->flags = 0;

   if (!job->rt_dataset->need_frag)
      state->flags |= PVR_WINSYS_GEOM_FLAG_FIRST_GEOMETRY;

   if (job->geometry_terminate)
      state->flags |= PVR_WINSYS_GEOM_FLAG_LAST_GEOMETRY;

   if (job->frag_uses_atomic_ops)
      state->flags |= PVR_WINSYS_GEOM_FLAG_SINGLE_CORE;
}

static inline void
pvr_get_isp_num_tiles_xy(const struct pvr_device_info *dev_info,
                         uint32_t samples,
                         uint32_t width,
                         uint32_t height,
                         uint32_t *const x_out,
                         uint32_t *const y_out)
{
   uint32_t tile_samples_x;
   uint32_t tile_samples_y;
   uint32_t scale_x;
   uint32_t scale_y;

   rogue_get_isp_samples_per_tile_xy(dev_info,
                                     samples,
                                     &tile_samples_x,
                                     &tile_samples_y);

   switch (samples) {
   case 1:
      scale_x = 1;
      scale_y = 1;
      break;
   case 2:
      scale_x = 1;
      scale_y = 2;
      break;
   case 4:
      scale_x = 2;
      scale_y = 2;
      break;
   case 8:
      scale_x = 2;
      scale_y = 4;
      break;
   default:
      unreachable("Unsupported number of samples");
   }

   *x_out = DIV_ROUND_UP(width * scale_x, tile_samples_x);
   *y_out = DIV_ROUND_UP(height * scale_y, tile_samples_y);

   if (PVR_HAS_FEATURE(dev_info, simple_internal_parameter_format)) {
      assert(PVR_GET_FEATURE_VALUE(dev_info,
                                   simple_parameter_format_version,
                                   0U) == 2U);
      /* Align to a 2x2 tile block. */
      *x_out = ALIGN_POT(*x_out, 2);
      *y_out = ALIGN_POT(*y_out, 2);
   }
}

static void
pvr_render_job_ws_fragment_state_init(struct pvr_render_ctx *ctx,
                                      struct pvr_render_job *job,
                                      struct pvr_winsys_fragment_state *state)
{
   const enum PVRX(CR_ISP_AA_MODE_TYPE)
      isp_aa_mode = pvr_cr_isp_aa_mode_type(job->samples);
   const struct pvr_device_runtime_info *dev_runtime_info =
      &ctx->device->pdevice->dev_runtime_info;
   const struct pvr_device_info *dev_info = &ctx->device->pdevice->dev_info;
   uint32_t isp_ctl;

   /* FIXME: what to do when job->run_frag is false? */

   /* FIXME: pass in the number of samples rather than isp_aa_mode? */
   pvr_setup_tiles_in_flight(dev_info,
                             dev_runtime_info,
                             isp_aa_mode,
                             job->pixel_output_width,
                             false,
                             job->max_tiles_in_flight,
                             &isp_ctl,
                             &state->regs.usc_pixel_output_ctrl);

   pvr_csb_pack (&state->regs.isp_ctl, CR_ISP_CTL, value) {
      value.sample_pos = true;

      /* FIXME: There are a number of things that cause this to be set, this
       * is just one of them.
       */
      value.process_empty_tiles = job->process_empty_tiles;
   }

   /* FIXME: When pvr_setup_tiles_in_flight() is refactored it might be
    * possible to fully pack CR_ISP_CTL above rather than having to OR in part
    * of the value.
    */
   state->regs.isp_ctl |= isp_ctl;

   pvr_csb_pack (&state->regs.isp_aa, CR_ISP_AA, value) {
      value.mode = isp_aa_mode;
   }

   /* The set up of CR_TPU must be identical to
    * pvr_render_job_ws_geometry_state_init().
    */
   pvr_csb_pack (&state->regs.tpu, CR_TPU, value) {
      value.tag_cem_4k_face_packing = true;
   }

   if (PVR_HAS_FEATURE(dev_info, cluster_grouping) &&
       PVR_HAS_FEATURE(dev_info, slc_mcu_cache_controls) &&
       dev_runtime_info->num_phantoms > 1 && job->frag_uses_atomic_ops) {
      /* Each phantom has its own MCU, so atomicity can only be guaranteed
       * when all work items are processed on the same phantom. This means we
       * need to disable all USCs other than those of the first phantom, which
       * has 4 clusters. Note that we only need to do this for atomic
       * operations in fragment shaders, since hardware prevents the TA to run
       * on more than one phantom anyway.
       */
      state->regs.pixel_phantom = 0xF;
   } else {
      state->regs.pixel_phantom = 0;
   }

   pvr_csb_pack (&state->regs.isp_bgobjvals, CR_ISP_BGOBJVALS, value) {
      value.enablebgtag = job->enable_bg_tag;

      value.mask = true;

      /* FIXME: Hard code this for now as we don't currently support any
       * stencil image formats.
       */
      value.stencil = 0xFF;
   }

   pvr_csb_pack (&state->regs.isp_bgobjdepth, CR_ISP_BGOBJDEPTH, value) {
      /* FIXME: This is suitable for the single depth format the driver
       * currently supports, but may need updating to handle other depth
       * formats.
       */
      value.value = fui(job->depth_clear_value);
   }

   /* FIXME: Some additional set up needed to support depth and stencil
    * load/store operations.
    */
   pvr_csb_pack (&state->regs.isp_zlsctl, CR_ISP_ZLSCTL, value) {
      uint32_t aligned_width =
         ALIGN_POT(job->depth_physical_width, ROGUE_IPF_TILE_SIZE_PIXELS);
      uint32_t aligned_height =
         ALIGN_POT(job->depth_physical_height, ROGUE_IPF_TILE_SIZE_PIXELS);

      pvr_get_isp_num_tiles_xy(dev_info,
                               job->samples,
                               aligned_width,
                               aligned_height,
                               &value.zlsextent_x_z,
                               &value.zlsextent_y_z);
      value.zlsextent_x_z -= 1;
      value.zlsextent_y_z -= 1;

      if (job->depth_memlayout == PVR_MEMLAYOUT_TWIDDLED) {
         value.loadtwiddled = true;
         value.storetwiddled = true;
      }

      /* FIXME: This is suitable for the single depth format the driver
       * currently supports, but may need updating to handle other depth
       * formats.
       */
      assert(job->depth_vk_format == VK_FORMAT_D32_SFLOAT);
      value.zloadformat = PVRX(CR_ZLOADFORMAT_TYPE_F32Z);
      value.zstoreformat = PVRX(CR_ZSTOREFORMAT_TYPE_F32Z);
   }

   if (PVR_HAS_FEATURE(dev_info, zls_subtile)) {
      pvr_csb_pack (&state->regs.isp_zls_pixels, CR_ISP_ZLS_PIXELS, value) {
         value.x = job->depth_stride - 1;
         value.y = job->depth_height - 1;
      }
   } else {
      state->regs.isp_zls_pixels = 0;
   }

   pvr_csb_pack (&state->regs.isp_zload_store_base, CR_ISP_ZLOAD_BASE, value) {
      value.addr = job->depth_addr;
   }

   pvr_csb_pack (&state->regs.isp_stencil_load_store_base,
                 CR_ISP_STENCIL_LOAD_BASE,
                 value) {
      value.addr = job->stencil_addr;

      /* FIXME: May need to set value.enable to true. */
   }

   pvr_csb_pack (&state->regs.tpu_border_colour_table,
                 CR_TPU_BORDER_COLOUR_TABLE_PDM,
                 value) {
      value.border_colour_table_address = job->border_colour_table_addr;
   }

   state->regs.isp_oclqry_base = 0;

   pvr_csb_pack (&state->regs.isp_dbias_base, CR_ISP_DBIAS_BASE, value) {
      value.addr = job->depth_bias_table_addr;
   }

   pvr_csb_pack (&state->regs.isp_scissor_base, CR_ISP_SCISSOR_BASE, value) {
      value.addr = job->scissor_table_addr;
   }

   pvr_csb_pack (&state->regs.event_pixel_pds_info,
                 CR_EVENT_PIXEL_PDS_INFO,
                 value) {
      value.const_size =
         DIV_ROUND_UP(ctx->device->pixel_event_data_size_in_dwords,
                      PVRX(CR_EVENT_PIXEL_PDS_INFO_CONST_SIZE_UNIT_SIZE));
      value.temp_stride = 0;
      value.usc_sr_size =
         DIV_ROUND_UP(PVR_STATE_PBE_DWORDS,
                      PVRX(CR_EVENT_PIXEL_PDS_INFO_USC_SR_SIZE_UNIT_SIZE));
   }

   pvr_csb_pack (&state->regs.event_pixel_pds_data,
                 CR_EVENT_PIXEL_PDS_DATA,
                 value) {
      value.addr = PVR_DEV_ADDR(job->pds_pixel_event_data_offset);
   }

   STATIC_ASSERT(ARRAY_SIZE(state->regs.pbe_word) ==
                 ARRAY_SIZE(job->pbe_reg_words));
   STATIC_ASSERT(ARRAY_SIZE(state->regs.pbe_word[0]) ==
                 ARRAY_SIZE(job->pbe_reg_words[0]));

   for (uint32_t i = 0; i < ARRAY_SIZE(job->pbe_reg_words); i++) {
      state->regs.pbe_word[i][0] = job->pbe_reg_words[i][0];
      state->regs.pbe_word[i][1] = job->pbe_reg_words[i][1];
      state->regs.pbe_word[i][2] = job->pbe_reg_words[i][2];
   }

   STATIC_ASSERT(__same_type(state->regs.pds_bgnd, job->pds_bgnd_reg_values));
   typed_memcpy(state->regs.pds_bgnd,
                job->pds_bgnd_reg_values,
                ARRAY_SIZE(state->regs.pds_bgnd));

   memset(state->regs.pds_pr_bgnd, 0, sizeof(state->regs.pds_pr_bgnd));

   /* FIXME: Merge geometry and fragment flags into a single flags member? */
   /* FIXME: move to its own function? */
   state->flags = 0;

   if (job->depth_addr.addr)
      state->flags |= PVR_WINSYS_FRAG_FLAG_DEPTH_BUFFER_PRESENT;

   if (job->stencil_addr.addr)
      state->flags |= PVR_WINSYS_FRAG_FLAG_STENCIL_BUFFER_PRESENT;

   if (job->disable_compute_overlap)
      state->flags |= PVR_WINSYS_FRAG_FLAG_PREVENT_CDM_OVERLAP;

   if (job->frag_uses_atomic_ops)
      state->flags |= PVR_WINSYS_FRAG_FLAG_SINGLE_CORE;

   state->zls_stride = job->depth_layer_size;
   state->sls_stride = job->depth_layer_size;
}

static void pvr_render_job_ws_submit_info_init(
   struct pvr_render_ctx *ctx,
   struct pvr_render_job *job,
   const struct pvr_winsys_job_bo *bos,
   uint32_t bo_count,
   struct vk_sync **waits,
   uint32_t wait_count,
   uint32_t *stage_flags,
   struct pvr_winsys_render_submit_info *submit_info)
{
   memset(submit_info, 0, sizeof(*submit_info));

   submit_info->rt_dataset = job->rt_dataset->ws_rt_dataset;
   submit_info->rt_data_idx = job->rt_dataset->rt_data_idx;

   submit_info->frame_num = ctx->device->global_queue_present_count;
   submit_info->job_num = ctx->device->global_queue_job_count;

   submit_info->run_frag = job->run_frag;

   submit_info->bos = bos;
   submit_info->bo_count = bo_count;

   submit_info->waits = waits;
   submit_info->wait_count = wait_count;
   submit_info->stage_flags = stage_flags;

   /* FIXME: add WSI image bos. */

   pvr_render_job_ws_geometry_state_init(ctx, job, &submit_info->geometry);
   pvr_render_job_ws_fragment_state_init(ctx, job, &submit_info->fragment);

   /* These values are expected to match. */
   assert(submit_info->geometry.regs.tpu == submit_info->fragment.regs.tpu);
}

VkResult pvr_render_job_submit(struct pvr_render_ctx *ctx,
                               struct pvr_render_job *job,
                               const struct pvr_winsys_job_bo *bos,
                               uint32_t bo_count,
                               struct vk_sync **waits,
                               uint32_t wait_count,
                               uint32_t *stage_flags,
                               struct vk_sync *signal_sync_geom,
                               struct vk_sync *signal_sync_frag)
{
   struct pvr_rt_dataset *rt_dataset = job->rt_dataset;
   struct pvr_winsys_render_submit_info submit_info;
   struct pvr_device *device = ctx->device;
   VkResult result;

   pvr_render_job_ws_submit_info_init(ctx,
                                      job,
                                      bos,
                                      bo_count,
                                      waits,
                                      wait_count,
                                      stage_flags,
                                      &submit_info);

   result = device->ws->ops->render_submit(ctx->ws_ctx,
                                           &submit_info,
                                           signal_sync_geom,
                                           signal_sync_frag);
   if (result != VK_SUCCESS)
      return result;

   if (job->run_frag) {
      /* Move to the next render target data now that a fragment job has been
       * successfully submitted. This will allow the next geometry job to be
       * submitted to been run in parallel with it.
       */
      rt_dataset->rt_data_idx =
         (rt_dataset->rt_data_idx + 1) % ARRAY_SIZE(rt_dataset->rt_datas);

      rt_dataset->need_frag = false;
   } else {
      rt_dataset->need_frag = true;
   }

   return VK_SUCCESS;
}
