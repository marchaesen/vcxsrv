/*
 * Copyright © 2011 Red Hat All Rights Reserved.
 * Copyright © 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include "ac_surface.h"

#include "ac_gpu_info.h"
#include "addrlib/inc/addrinterface.h"
#include "addrlib/src/amdgpu_asic_addr.h"
#include "amd_family.h"
#include "drm-uapi/amdgpu_drm.h"
#include "sid.h"
#include "util/hash_table.h"
#include "util/macros.h"
#include "util/simple_mtx.h"
#include "util/u_atomic.h"
#include "util/u_math.h"
#include "util/u_memory.h"

#include <amdgpu.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef CIASICIDGFXENGINE_SOUTHERNISLAND
#define CIASICIDGFXENGINE_SOUTHERNISLAND 0x0000000A
#endif

#ifndef CIASICIDGFXENGINE_ARCTICISLAND
#define CIASICIDGFXENGINE_ARCTICISLAND 0x0000000D
#endif

struct ac_addrlib {
   ADDR_HANDLE handle;

   /* The cache of DCC retile maps for reuse when allocating images of
    * similar sizes.
    */
   simple_mtx_t dcc_retile_map_lock;
   struct hash_table *dcc_retile_maps;
   struct hash_table *dcc_retile_tile_indices;
};

struct dcc_retile_map_key {
   enum radeon_family family;
   unsigned retile_width;
   unsigned retile_height;
   bool rb_aligned;
   bool pipe_aligned;
   unsigned dcc_retile_num_elements;
   ADDR2_COMPUTE_DCC_ADDRFROMCOORD_INPUT input;
};

static uint32_t dcc_retile_map_hash_key(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct dcc_retile_map_key));
}

static bool dcc_retile_map_keys_equal(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct dcc_retile_map_key)) == 0;
}

static void dcc_retile_map_free(struct hash_entry *entry)
{
   free((void *)entry->key);
   free(entry->data);
}

struct dcc_retile_tile_key {
   enum radeon_family family;
   unsigned bpp;
   unsigned swizzle_mode;
   bool rb_aligned;
   bool pipe_aligned;
};

struct dcc_retile_tile_data {
   unsigned tile_width_log2;
   unsigned tile_height_log2;
   uint16_t *data;
};

static uint32_t dcc_retile_tile_hash_key(const void *key)
{
   return _mesa_hash_data(key, sizeof(struct dcc_retile_tile_key));
}

static bool dcc_retile_tile_keys_equal(const void *a, const void *b)
{
   return memcmp(a, b, sizeof(struct dcc_retile_tile_key)) == 0;
}

static void dcc_retile_tile_free(struct hash_entry *entry)
{
   free((void *)entry->key);
   free(((struct dcc_retile_tile_data *)entry->data)->data);
   free(entry->data);
}

/* Assumes dcc_retile_map_lock is taken. */
static const struct dcc_retile_tile_data *
ac_compute_dcc_retile_tile_indices(struct ac_addrlib *addrlib, const struct radeon_info *info,
                                   unsigned bpp, unsigned swizzle_mode, bool rb_aligned,
                                   bool pipe_aligned)
{
   struct dcc_retile_tile_key key;
   memset(&key, 0, sizeof(key));

   key.family = info->family;
   key.bpp = bpp;
   key.swizzle_mode = swizzle_mode;
   key.rb_aligned = rb_aligned;
   key.pipe_aligned = pipe_aligned;

   struct hash_entry *entry = _mesa_hash_table_search(addrlib->dcc_retile_tile_indices, &key);
   if (entry)
      return entry->data;

   ADDR2_COMPUTE_DCCINFO_INPUT din = {0};
   ADDR2_COMPUTE_DCCINFO_OUTPUT dout = {0};
   din.size = sizeof(ADDR2_COMPUTE_DCCINFO_INPUT);
   dout.size = sizeof(ADDR2_COMPUTE_DCCINFO_OUTPUT);

   din.dccKeyFlags.pipeAligned = pipe_aligned;
   din.dccKeyFlags.rbAligned = rb_aligned;
   din.resourceType = ADDR_RSRC_TEX_2D;
   din.swizzleMode = swizzle_mode;
   din.bpp = bpp;
   din.unalignedWidth = 1;
   din.unalignedHeight = 1;
   din.numSlices = 1;
   din.numFrags = 1;
   din.numMipLevels = 1;

   ADDR_E_RETURNCODE ret = Addr2ComputeDccInfo(addrlib->handle, &din, &dout);
   if (ret != ADDR_OK)
      return NULL;

   ADDR2_COMPUTE_DCC_ADDRFROMCOORD_INPUT addrin = {0};
   addrin.size = sizeof(addrin);
   addrin.swizzleMode = swizzle_mode;
   addrin.resourceType = ADDR_RSRC_TEX_2D;
   addrin.bpp = bpp;
   addrin.numSlices = 1;
   addrin.numMipLevels = 1;
   addrin.numFrags = 1;
   addrin.pitch = dout.pitch;
   addrin.height = dout.height;
   addrin.compressBlkWidth = dout.compressBlkWidth;
   addrin.compressBlkHeight = dout.compressBlkHeight;
   addrin.compressBlkDepth = dout.compressBlkDepth;
   addrin.metaBlkWidth = dout.metaBlkWidth;
   addrin.metaBlkHeight = dout.metaBlkHeight;
   addrin.metaBlkDepth = dout.metaBlkDepth;
   addrin.dccKeyFlags.pipeAligned = pipe_aligned;
   addrin.dccKeyFlags.rbAligned = rb_aligned;

   unsigned w = dout.metaBlkWidth / dout.compressBlkWidth;
   unsigned h = dout.metaBlkHeight / dout.compressBlkHeight;
   uint16_t *indices = malloc(w * h * sizeof(uint16_t));
   if (!indices)
      return NULL;

   ADDR2_COMPUTE_DCC_ADDRFROMCOORD_OUTPUT addrout = {0};
   addrout.size = sizeof(addrout);

   for (unsigned y = 0; y < h; ++y) {
      addrin.y = y * dout.compressBlkHeight;
      for (unsigned x = 0; x < w; ++x) {
         addrin.x = x * dout.compressBlkWidth;
         addrout.addr = 0;

         if (Addr2ComputeDccAddrFromCoord(addrlib->handle, &addrin, &addrout) != ADDR_OK) {
            free(indices);
            return NULL;
         }
         indices[y * w + x] = addrout.addr;
      }
   }

   struct dcc_retile_tile_data *data = calloc(1, sizeof(*data));
   if (!data) {
      free(indices);
      return NULL;
   }

   data->tile_width_log2 = util_logbase2(w);
   data->tile_height_log2 = util_logbase2(h);
   data->data = indices;

   struct dcc_retile_tile_key *heap_key = mem_dup(&key, sizeof(key));
   if (!heap_key) {
      free(data);
      free(indices);
      return NULL;
   }

   entry = _mesa_hash_table_insert(addrlib->dcc_retile_tile_indices, heap_key, data);
   if (!entry) {
      free(heap_key);
      free(data);
      free(indices);
   }
   return data;
}

static uint32_t ac_compute_retile_tile_addr(const struct dcc_retile_tile_data *tile,
                                            unsigned stride, unsigned x, unsigned y)
{
   unsigned x_mask = (1u << tile->tile_width_log2) - 1;
   unsigned y_mask = (1u << tile->tile_height_log2) - 1;
   unsigned tile_size_log2 = tile->tile_width_log2 + tile->tile_height_log2;

   unsigned base = ((y >> tile->tile_height_log2) * stride + (x >> tile->tile_width_log2))
                   << tile_size_log2;
   unsigned offset_in_tile = tile->data[((y & y_mask) << tile->tile_width_log2) + (x & x_mask)];
   return base + offset_in_tile;
}

static uint32_t *ac_compute_dcc_retile_map(struct ac_addrlib *addrlib,
                                           const struct radeon_info *info, unsigned retile_width,
                                           unsigned retile_height, bool rb_aligned,
                                           bool pipe_aligned, bool use_uint16,
                                           unsigned dcc_retile_num_elements,
                                           const ADDR2_COMPUTE_DCC_ADDRFROMCOORD_INPUT *in)
{
   unsigned dcc_retile_map_size = dcc_retile_num_elements * (use_uint16 ? 2 : 4);
   struct dcc_retile_map_key key;

   assert(in->numFrags == 1 && in->numSlices == 1 && in->numMipLevels == 1);

   memset(&key, 0, sizeof(key));
   key.family = info->family;
   key.retile_width = retile_width;
   key.retile_height = retile_height;
   key.rb_aligned = rb_aligned;
   key.pipe_aligned = pipe_aligned;
   key.dcc_retile_num_elements = dcc_retile_num_elements;
   memcpy(&key.input, in, sizeof(*in));

   simple_mtx_lock(&addrlib->dcc_retile_map_lock);

   /* If we have already computed this retile map, get it from the hash table. */
   struct hash_entry *entry = _mesa_hash_table_search(addrlib->dcc_retile_maps, &key);
   if (entry) {
      uint32_t *map = entry->data;
      simple_mtx_unlock(&addrlib->dcc_retile_map_lock);
      return map;
   }

   const struct dcc_retile_tile_data *src_tile = ac_compute_dcc_retile_tile_indices(
      addrlib, info, in->bpp, in->swizzleMode, rb_aligned, pipe_aligned);
   const struct dcc_retile_tile_data *dst_tile =
      ac_compute_dcc_retile_tile_indices(addrlib, info, in->bpp, in->swizzleMode, false, false);
   if (!src_tile || !dst_tile) {
      simple_mtx_unlock(&addrlib->dcc_retile_map_lock);
      return NULL;
   }

   void *dcc_retile_map = malloc(dcc_retile_map_size);
   if (!dcc_retile_map) {
      simple_mtx_unlock(&addrlib->dcc_retile_map_lock);
      return NULL;
   }

   unsigned index = 0;
   unsigned w = DIV_ROUND_UP(retile_width, in->compressBlkWidth);
   unsigned h = DIV_ROUND_UP(retile_height, in->compressBlkHeight);
   unsigned src_stride = DIV_ROUND_UP(w, 1u << src_tile->tile_width_log2);
   unsigned dst_stride = DIV_ROUND_UP(w, 1u << dst_tile->tile_width_log2);

   for (unsigned y = 0; y < h; ++y) {
      for (unsigned x = 0; x < w; ++x) {
         unsigned src_addr = ac_compute_retile_tile_addr(src_tile, src_stride, x, y);
         unsigned dst_addr = ac_compute_retile_tile_addr(dst_tile, dst_stride, x, y);

         if (use_uint16) {
            ((uint16_t *)dcc_retile_map)[2 * index] = src_addr;
            ((uint16_t *)dcc_retile_map)[2 * index + 1] = dst_addr;
         } else {
            ((uint32_t *)dcc_retile_map)[2 * index] = src_addr;
            ((uint32_t *)dcc_retile_map)[2 * index + 1] = dst_addr;
         }
         ++index;
      }
   }

   /* Fill the remaining pairs with the last one (for the compute shader). */
   for (unsigned i = index * 2; i < dcc_retile_num_elements; i++) {
      if (use_uint16)
         ((uint16_t *)dcc_retile_map)[i] = ((uint16_t *)dcc_retile_map)[i - 2];
      else
         ((uint32_t *)dcc_retile_map)[i] = ((uint32_t *)dcc_retile_map)[i - 2];
   }

   /* Insert the retile map into the hash table, so that it can be reused and
    * the computation can be skipped for similar image sizes.
    */
   _mesa_hash_table_insert(addrlib->dcc_retile_maps, mem_dup(&key, sizeof(key)), dcc_retile_map);

   simple_mtx_unlock(&addrlib->dcc_retile_map_lock);
   return dcc_retile_map;
}

static void *ADDR_API allocSysMem(const ADDR_ALLOCSYSMEM_INPUT *pInput)
{
   return malloc(pInput->sizeInBytes);
}

static ADDR_E_RETURNCODE ADDR_API freeSysMem(const ADDR_FREESYSMEM_INPUT *pInput)
{
   free(pInput->pVirtAddr);
   return ADDR_OK;
}

struct ac_addrlib *ac_addrlib_create(const struct radeon_info *info,
                                     const struct amdgpu_gpu_info *amdinfo, uint64_t *max_alignment)
{
   ADDR_CREATE_INPUT addrCreateInput = {0};
   ADDR_CREATE_OUTPUT addrCreateOutput = {0};
   ADDR_REGISTER_VALUE regValue = {0};
   ADDR_CREATE_FLAGS createFlags = {{0}};
   ADDR_GET_MAX_ALIGNMENTS_OUTPUT addrGetMaxAlignmentsOutput = {0};
   ADDR_E_RETURNCODE addrRet;

   addrCreateInput.size = sizeof(ADDR_CREATE_INPUT);
   addrCreateOutput.size = sizeof(ADDR_CREATE_OUTPUT);

   regValue.gbAddrConfig = amdinfo->gb_addr_cfg;
   createFlags.value = 0;

   addrCreateInput.chipFamily = info->family_id;
   addrCreateInput.chipRevision = info->chip_external_rev;

   if (addrCreateInput.chipFamily == FAMILY_UNKNOWN)
      return NULL;

   if (addrCreateInput.chipFamily >= FAMILY_AI) {
      addrCreateInput.chipEngine = CIASICIDGFXENGINE_ARCTICISLAND;
   } else {
      regValue.noOfBanks = amdinfo->mc_arb_ramcfg & 0x3;
      regValue.noOfRanks = (amdinfo->mc_arb_ramcfg & 0x4) >> 2;

      regValue.backendDisables = amdinfo->enabled_rb_pipes_mask;
      regValue.pTileConfig = amdinfo->gb_tile_mode;
      regValue.noOfEntries = ARRAY_SIZE(amdinfo->gb_tile_mode);
      if (addrCreateInput.chipFamily == FAMILY_SI) {
         regValue.pMacroTileConfig = NULL;
         regValue.noOfMacroEntries = 0;
      } else {
         regValue.pMacroTileConfig = amdinfo->gb_macro_tile_mode;
         regValue.noOfMacroEntries = ARRAY_SIZE(amdinfo->gb_macro_tile_mode);
      }

      createFlags.useTileIndex = 1;
      createFlags.useHtileSliceAlign = 1;

      addrCreateInput.chipEngine = CIASICIDGFXENGINE_SOUTHERNISLAND;
   }

   addrCreateInput.callbacks.allocSysMem = allocSysMem;
   addrCreateInput.callbacks.freeSysMem = freeSysMem;
   addrCreateInput.callbacks.debugPrint = 0;
   addrCreateInput.createFlags = createFlags;
   addrCreateInput.regValue = regValue;

   addrRet = AddrCreate(&addrCreateInput, &addrCreateOutput);
   if (addrRet != ADDR_OK)
      return NULL;

   if (max_alignment) {
      addrRet = AddrGetMaxAlignments(addrCreateOutput.hLib, &addrGetMaxAlignmentsOutput);
      if (addrRet == ADDR_OK) {
         *max_alignment = addrGetMaxAlignmentsOutput.baseAlign;
      }
   }

   struct ac_addrlib *addrlib = calloc(1, sizeof(struct ac_addrlib));
   if (!addrlib) {
      AddrDestroy(addrCreateOutput.hLib);
      return NULL;
   }

   addrlib->handle = addrCreateOutput.hLib;
   simple_mtx_init(&addrlib->dcc_retile_map_lock, mtx_plain);
   addrlib->dcc_retile_maps =
      _mesa_hash_table_create(NULL, dcc_retile_map_hash_key, dcc_retile_map_keys_equal);
   addrlib->dcc_retile_tile_indices =
      _mesa_hash_table_create(NULL, dcc_retile_tile_hash_key, dcc_retile_tile_keys_equal);
   return addrlib;
}

void ac_addrlib_destroy(struct ac_addrlib *addrlib)
{
   AddrDestroy(addrlib->handle);
   simple_mtx_destroy(&addrlib->dcc_retile_map_lock);
   _mesa_hash_table_destroy(addrlib->dcc_retile_maps, dcc_retile_map_free);
   _mesa_hash_table_destroy(addrlib->dcc_retile_tile_indices, dcc_retile_tile_free);
   free(addrlib);
}

static int surf_config_sanity(const struct ac_surf_config *config, unsigned flags)
{
   /* FMASK is allocated together with the color surface and can't be
    * allocated separately.
    */
   assert(!(flags & RADEON_SURF_FMASK));
   if (flags & RADEON_SURF_FMASK)
      return -EINVAL;

   /* all dimension must be at least 1 ! */
   if (!config->info.width || !config->info.height || !config->info.depth ||
       !config->info.array_size || !config->info.levels)
      return -EINVAL;

   switch (config->info.samples) {
   case 0:
   case 1:
   case 2:
   case 4:
   case 8:
      break;
   case 16:
      if (flags & RADEON_SURF_Z_OR_SBUFFER)
         return -EINVAL;
      break;
   default:
      return -EINVAL;
   }

   if (!(flags & RADEON_SURF_Z_OR_SBUFFER)) {
      switch (config->info.storage_samples) {
      case 0:
      case 1:
      case 2:
      case 4:
      case 8:
         break;
      default:
         return -EINVAL;
      }
   }

   if (config->is_3d && config->info.array_size > 1)
      return -EINVAL;
   if (config->is_cube && config->info.depth > 1)
      return -EINVAL;

   return 0;
}

static int gfx6_compute_level(ADDR_HANDLE addrlib, const struct ac_surf_config *config,
                              struct radeon_surf *surf, bool is_stencil, unsigned level,
                              bool compressed, ADDR_COMPUTE_SURFACE_INFO_INPUT *AddrSurfInfoIn,
                              ADDR_COMPUTE_SURFACE_INFO_OUTPUT *AddrSurfInfoOut,
                              ADDR_COMPUTE_DCCINFO_INPUT *AddrDccIn,
                              ADDR_COMPUTE_DCCINFO_OUTPUT *AddrDccOut,
                              ADDR_COMPUTE_HTILE_INFO_INPUT *AddrHtileIn,
                              ADDR_COMPUTE_HTILE_INFO_OUTPUT *AddrHtileOut)
{
   struct legacy_surf_level *surf_level;
   ADDR_E_RETURNCODE ret;

   AddrSurfInfoIn->mipLevel = level;
   AddrSurfInfoIn->width = u_minify(config->info.width, level);
   AddrSurfInfoIn->height = u_minify(config->info.height, level);

   /* Make GFX6 linear surfaces compatible with GFX9 for hybrid graphics,
    * because GFX9 needs linear alignment of 256 bytes.
    */
   if (config->info.levels == 1 && AddrSurfInfoIn->tileMode == ADDR_TM_LINEAR_ALIGNED &&
       AddrSurfInfoIn->bpp && util_is_power_of_two_or_zero(AddrSurfInfoIn->bpp)) {
      unsigned alignment = 256 / (AddrSurfInfoIn->bpp / 8);

      AddrSurfInfoIn->width = align(AddrSurfInfoIn->width, alignment);
   }

   /* addrlib assumes the bytes/pixel is a divisor of 64, which is not
    * true for r32g32b32 formats. */
   if (AddrSurfInfoIn->bpp == 96) {
      assert(config->info.levels == 1);
      assert(AddrSurfInfoIn->tileMode == ADDR_TM_LINEAR_ALIGNED);

      /* The least common multiple of 64 bytes and 12 bytes/pixel is
       * 192 bytes, or 16 pixels. */
      AddrSurfInfoIn->width = align(AddrSurfInfoIn->width, 16);
   }

   if (config->is_3d)
      AddrSurfInfoIn->numSlices = u_minify(config->info.depth, level);
   else if (config->is_cube)
      AddrSurfInfoIn->numSlices = 6;
   else
      AddrSurfInfoIn->numSlices = config->info.array_size;

   if (level > 0) {
      /* Set the base level pitch. This is needed for calculation
       * of non-zero levels. */
      if (is_stencil)
         AddrSurfInfoIn->basePitch = surf->u.legacy.stencil_level[0].nblk_x;
      else
         AddrSurfInfoIn->basePitch = surf->u.legacy.level[0].nblk_x;

      /* Convert blocks to pixels for compressed formats. */
      if (compressed)
         AddrSurfInfoIn->basePitch *= surf->blk_w;
   }

   ret = AddrComputeSurfaceInfo(addrlib, AddrSurfInfoIn, AddrSurfInfoOut);
   if (ret != ADDR_OK) {
      return ret;
   }

   surf_level = is_stencil ? &surf->u.legacy.stencil_level[level] : &surf->u.legacy.level[level];
   surf_level->offset = align64(surf->surf_size, AddrSurfInfoOut->baseAlign);
   surf_level->slice_size_dw = AddrSurfInfoOut->sliceSize / 4;
   surf_level->nblk_x = AddrSurfInfoOut->pitch;
   surf_level->nblk_y = AddrSurfInfoOut->height;

   switch (AddrSurfInfoOut->tileMode) {
   case ADDR_TM_LINEAR_ALIGNED:
      surf_level->mode = RADEON_SURF_MODE_LINEAR_ALIGNED;
      break;
   case ADDR_TM_1D_TILED_THIN1:
      surf_level->mode = RADEON_SURF_MODE_1D;
      break;
   case ADDR_TM_2D_TILED_THIN1:
      surf_level->mode = RADEON_SURF_MODE_2D;
      break;
   default:
      assert(0);
   }

   if (is_stencil)
      surf->u.legacy.stencil_tiling_index[level] = AddrSurfInfoOut->tileIndex;
   else
      surf->u.legacy.tiling_index[level] = AddrSurfInfoOut->tileIndex;

   surf->surf_size = surf_level->offset + AddrSurfInfoOut->surfSize;

   /* Clear DCC fields at the beginning. */
   surf_level->dcc_offset = 0;

   /* The previous level's flag tells us if we can use DCC for this level. */
   if (AddrSurfInfoIn->flags.dccCompatible && (level == 0 || AddrDccOut->subLvlCompressible)) {
      bool prev_level_clearable = level == 0 || AddrDccOut->dccRamSizeAligned;

      AddrDccIn->colorSurfSize = AddrSurfInfoOut->surfSize;
      AddrDccIn->tileMode = AddrSurfInfoOut->tileMode;
      AddrDccIn->tileInfo = *AddrSurfInfoOut->pTileInfo;
      AddrDccIn->tileIndex = AddrSurfInfoOut->tileIndex;
      AddrDccIn->macroModeIndex = AddrSurfInfoOut->macroModeIndex;

      ret = AddrComputeDccInfo(addrlib, AddrDccIn, AddrDccOut);

      if (ret == ADDR_OK) {
         surf_level->dcc_offset = surf->dcc_size;
         surf->num_dcc_levels = level + 1;
         surf->dcc_size = surf_level->dcc_offset + AddrDccOut->dccRamSize;
         surf->dcc_alignment = MAX2(surf->dcc_alignment, AddrDccOut->dccRamBaseAlign);

         /* If the DCC size of a subresource (1 mip level or 1 slice)
          * is not aligned, the DCC memory layout is not contiguous for
          * that subresource, which means we can't use fast clear.
          *
          * We only do fast clears for whole mipmap levels. If we did
          * per-slice fast clears, the same restriction would apply.
          * (i.e. only compute the slice size and see if it's aligned)
          *
          * The last level can be non-contiguous and still be clearable
          * if it's interleaved with the next level that doesn't exist.
          */
         if (AddrDccOut->dccRamSizeAligned ||
             (prev_level_clearable && level == config->info.levels - 1))
            surf_level->dcc_fast_clear_size = AddrDccOut->dccFastClearSize;
         else
            surf_level->dcc_fast_clear_size = 0;

         /* Compute the DCC slice size because addrlib doesn't
          * provide this info. As DCC memory is linear (each
          * slice is the same size) it's easy to compute.
          */
         surf->dcc_slice_size = AddrDccOut->dccRamSize / config->info.array_size;

         /* For arrays, we have to compute the DCC info again
          * with one slice size to get a correct fast clear
          * size.
          */
         if (config->info.array_size > 1) {
            AddrDccIn->colorSurfSize = AddrSurfInfoOut->sliceSize;
            AddrDccIn->tileMode = AddrSurfInfoOut->tileMode;
            AddrDccIn->tileInfo = *AddrSurfInfoOut->pTileInfo;
            AddrDccIn->tileIndex = AddrSurfInfoOut->tileIndex;
            AddrDccIn->macroModeIndex = AddrSurfInfoOut->macroModeIndex;

            ret = AddrComputeDccInfo(addrlib, AddrDccIn, AddrDccOut);
            if (ret == ADDR_OK) {
               /* If the DCC memory isn't properly
                * aligned, the data are interleaved
                * accross slices.
                */
               if (AddrDccOut->dccRamSizeAligned)
                  surf_level->dcc_slice_fast_clear_size = AddrDccOut->dccFastClearSize;
               else
                  surf_level->dcc_slice_fast_clear_size = 0;
            }

            if (surf->flags & RADEON_SURF_CONTIGUOUS_DCC_LAYERS &&
                surf->dcc_slice_size != surf_level->dcc_slice_fast_clear_size) {
               surf->dcc_size = 0;
               surf->num_dcc_levels = 0;
               AddrDccOut->subLvlCompressible = false;
            }
         } else {
            surf_level->dcc_slice_fast_clear_size = surf_level->dcc_fast_clear_size;
         }
      }
   }

   /* HTILE. */
   if (!is_stencil && AddrSurfInfoIn->flags.depth && surf_level->mode == RADEON_SURF_MODE_2D &&
       level == 0 && !(surf->flags & RADEON_SURF_NO_HTILE)) {
      AddrHtileIn->flags.tcCompatible = AddrSurfInfoOut->tcCompatible;
      AddrHtileIn->pitch = AddrSurfInfoOut->pitch;
      AddrHtileIn->height = AddrSurfInfoOut->height;
      AddrHtileIn->numSlices = AddrSurfInfoOut->depth;
      AddrHtileIn->blockWidth = ADDR_HTILE_BLOCKSIZE_8;
      AddrHtileIn->blockHeight = ADDR_HTILE_BLOCKSIZE_8;
      AddrHtileIn->pTileInfo = AddrSurfInfoOut->pTileInfo;
      AddrHtileIn->tileIndex = AddrSurfInfoOut->tileIndex;
      AddrHtileIn->macroModeIndex = AddrSurfInfoOut->macroModeIndex;

      ret = AddrComputeHtileInfo(addrlib, AddrHtileIn, AddrHtileOut);

      if (ret == ADDR_OK) {
         surf->htile_size = AddrHtileOut->htileBytes;
         surf->htile_slice_size = AddrHtileOut->sliceSize;
         surf->htile_alignment = AddrHtileOut->baseAlign;
      }
   }

   return 0;
}

static void gfx6_set_micro_tile_mode(struct radeon_surf *surf, const struct radeon_info *info)
{
   uint32_t tile_mode = info->si_tile_mode_array[surf->u.legacy.tiling_index[0]];

   if (info->chip_class >= GFX7)
      surf->micro_tile_mode = G_009910_MICRO_TILE_MODE_NEW(tile_mode);
   else
      surf->micro_tile_mode = G_009910_MICRO_TILE_MODE(tile_mode);
}

static unsigned cik_get_macro_tile_index(struct radeon_surf *surf)
{
   unsigned index, tileb;

   tileb = 8 * 8 * surf->bpe;
   tileb = MIN2(surf->u.legacy.tile_split, tileb);

   for (index = 0; tileb > 64; index++)
      tileb >>= 1;

   assert(index < 16);
   return index;
}

static bool get_display_flag(const struct ac_surf_config *config, const struct radeon_surf *surf)
{
   unsigned num_channels = config->info.num_channels;
   unsigned bpe = surf->bpe;

   if (!config->is_3d && !config->is_cube && !(surf->flags & RADEON_SURF_Z_OR_SBUFFER) &&
       surf->flags & RADEON_SURF_SCANOUT && config->info.samples <= 1 && surf->blk_w <= 2 &&
       surf->blk_h == 1) {
      /* subsampled */
      if (surf->blk_w == 2 && surf->blk_h == 1)
         return true;

      if (/* RGBA8 or RGBA16F */
          (bpe >= 4 && bpe <= 8 && num_channels == 4) ||
          /* R5G6B5 or R5G5B5A1 */
          (bpe == 2 && num_channels >= 3) ||
          /* C8 palette */
          (bpe == 1 && num_channels == 1))
         return true;
   }
   return false;
}

/**
 * This must be called after the first level is computed.
 *
 * Copy surface-global settings like pipe/bank config from level 0 surface
 * computation, and compute tile swizzle.
 */
static int gfx6_surface_settings(ADDR_HANDLE addrlib, const struct radeon_info *info,
                                 const struct ac_surf_config *config,
                                 ADDR_COMPUTE_SURFACE_INFO_OUTPUT *csio, struct radeon_surf *surf)
{
   surf->surf_alignment = csio->baseAlign;
   surf->u.legacy.pipe_config = csio->pTileInfo->pipeConfig - 1;
   gfx6_set_micro_tile_mode(surf, info);

   /* For 2D modes only. */
   if (csio->tileMode >= ADDR_TM_2D_TILED_THIN1) {
      surf->u.legacy.bankw = csio->pTileInfo->bankWidth;
      surf->u.legacy.bankh = csio->pTileInfo->bankHeight;
      surf->u.legacy.mtilea = csio->pTileInfo->macroAspectRatio;
      surf->u.legacy.tile_split = csio->pTileInfo->tileSplitBytes;
      surf->u.legacy.num_banks = csio->pTileInfo->banks;
      surf->u.legacy.macro_tile_index = csio->macroModeIndex;
   } else {
      surf->u.legacy.macro_tile_index = 0;
   }

   /* Compute tile swizzle. */
   /* TODO: fix tile swizzle with mipmapping for GFX6 */
   if ((info->chip_class >= GFX7 || config->info.levels == 1) && config->info.surf_index &&
       surf->u.legacy.level[0].mode == RADEON_SURF_MODE_2D &&
       !(surf->flags & (RADEON_SURF_Z_OR_SBUFFER | RADEON_SURF_SHAREABLE)) &&
       !get_display_flag(config, surf)) {
      ADDR_COMPUTE_BASE_SWIZZLE_INPUT AddrBaseSwizzleIn = {0};
      ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT AddrBaseSwizzleOut = {0};

      AddrBaseSwizzleIn.size = sizeof(ADDR_COMPUTE_BASE_SWIZZLE_INPUT);
      AddrBaseSwizzleOut.size = sizeof(ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT);

      AddrBaseSwizzleIn.surfIndex = p_atomic_inc_return(config->info.surf_index) - 1;
      AddrBaseSwizzleIn.tileIndex = csio->tileIndex;
      AddrBaseSwizzleIn.macroModeIndex = csio->macroModeIndex;
      AddrBaseSwizzleIn.pTileInfo = csio->pTileInfo;
      AddrBaseSwizzleIn.tileMode = csio->tileMode;

      int r = AddrComputeBaseSwizzle(addrlib, &AddrBaseSwizzleIn, &AddrBaseSwizzleOut);
      if (r != ADDR_OK)
         return r;

      assert(AddrBaseSwizzleOut.tileSwizzle <=
             u_bit_consecutive(0, sizeof(surf->tile_swizzle) * 8));
      surf->tile_swizzle = AddrBaseSwizzleOut.tileSwizzle;
   }
   return 0;
}

static void ac_compute_cmask(const struct radeon_info *info, const struct ac_surf_config *config,
                             struct radeon_surf *surf)
{
   unsigned pipe_interleave_bytes = info->pipe_interleave_bytes;
   unsigned num_pipes = info->num_tile_pipes;
   unsigned cl_width, cl_height;

   if (surf->flags & RADEON_SURF_Z_OR_SBUFFER || surf->is_linear ||
       (config->info.samples >= 2 && !surf->fmask_size))
      return;

   assert(info->chip_class <= GFX8);

   switch (num_pipes) {
   case 2:
      cl_width = 32;
      cl_height = 16;
      break;
   case 4:
      cl_width = 32;
      cl_height = 32;
      break;
   case 8:
      cl_width = 64;
      cl_height = 32;
      break;
   case 16: /* Hawaii */
      cl_width = 64;
      cl_height = 64;
      break;
   default:
      assert(0);
      return;
   }

   unsigned base_align = num_pipes * pipe_interleave_bytes;

   unsigned width = align(surf->u.legacy.level[0].nblk_x, cl_width * 8);
   unsigned height = align(surf->u.legacy.level[0].nblk_y, cl_height * 8);
   unsigned slice_elements = (width * height) / (8 * 8);

   /* Each element of CMASK is a nibble. */
   unsigned slice_bytes = slice_elements / 2;

   surf->u.legacy.cmask_slice_tile_max = (width * height) / (128 * 128);
   if (surf->u.legacy.cmask_slice_tile_max)
      surf->u.legacy.cmask_slice_tile_max -= 1;

   unsigned num_layers;
   if (config->is_3d)
      num_layers = config->info.depth;
   else if (config->is_cube)
      num_layers = 6;
   else
      num_layers = config->info.array_size;

   surf->cmask_alignment = MAX2(256, base_align);
   surf->cmask_slice_size = align(slice_bytes, base_align);
   surf->cmask_size = surf->cmask_slice_size * num_layers;
}

/**
 * Fill in the tiling information in \p surf based on the given surface config.
 *
 * The following fields of \p surf must be initialized by the caller:
 * blk_w, blk_h, bpe, flags.
 */
static int gfx6_compute_surface(ADDR_HANDLE addrlib, const struct radeon_info *info,
                                const struct ac_surf_config *config, enum radeon_surf_mode mode,
                                struct radeon_surf *surf)
{
   unsigned level;
   bool compressed;
   ADDR_COMPUTE_SURFACE_INFO_INPUT AddrSurfInfoIn = {0};
   ADDR_COMPUTE_SURFACE_INFO_OUTPUT AddrSurfInfoOut = {0};
   ADDR_COMPUTE_DCCINFO_INPUT AddrDccIn = {0};
   ADDR_COMPUTE_DCCINFO_OUTPUT AddrDccOut = {0};
   ADDR_COMPUTE_HTILE_INFO_INPUT AddrHtileIn = {0};
   ADDR_COMPUTE_HTILE_INFO_OUTPUT AddrHtileOut = {0};
   ADDR_TILEINFO AddrTileInfoIn = {0};
   ADDR_TILEINFO AddrTileInfoOut = {0};
   int r;

   AddrSurfInfoIn.size = sizeof(ADDR_COMPUTE_SURFACE_INFO_INPUT);
   AddrSurfInfoOut.size = sizeof(ADDR_COMPUTE_SURFACE_INFO_OUTPUT);
   AddrDccIn.size = sizeof(ADDR_COMPUTE_DCCINFO_INPUT);
   AddrDccOut.size = sizeof(ADDR_COMPUTE_DCCINFO_OUTPUT);
   AddrHtileIn.size = sizeof(ADDR_COMPUTE_HTILE_INFO_INPUT);
   AddrHtileOut.size = sizeof(ADDR_COMPUTE_HTILE_INFO_OUTPUT);
   AddrSurfInfoOut.pTileInfo = &AddrTileInfoOut;

   compressed = surf->blk_w == 4 && surf->blk_h == 4;

   /* MSAA requires 2D tiling. */
   if (config->info.samples > 1)
      mode = RADEON_SURF_MODE_2D;

   /* DB doesn't support linear layouts. */
   if (surf->flags & (RADEON_SURF_Z_OR_SBUFFER) && mode < RADEON_SURF_MODE_1D)
      mode = RADEON_SURF_MODE_1D;

   /* Set the requested tiling mode. */
   switch (mode) {
   case RADEON_SURF_MODE_LINEAR_ALIGNED:
      AddrSurfInfoIn.tileMode = ADDR_TM_LINEAR_ALIGNED;
      break;
   case RADEON_SURF_MODE_1D:
      AddrSurfInfoIn.tileMode = ADDR_TM_1D_TILED_THIN1;
      break;
   case RADEON_SURF_MODE_2D:
      AddrSurfInfoIn.tileMode = ADDR_TM_2D_TILED_THIN1;
      break;
   default:
      assert(0);
   }

   /* The format must be set correctly for the allocation of compressed
    * textures to work. In other cases, setting the bpp is sufficient.
    */
   if (compressed) {
      switch (surf->bpe) {
      case 8:
         AddrSurfInfoIn.format = ADDR_FMT_BC1;
         break;
      case 16:
         AddrSurfInfoIn.format = ADDR_FMT_BC3;
         break;
      default:
         assert(0);
      }
   } else {
      AddrDccIn.bpp = AddrSurfInfoIn.bpp = surf->bpe * 8;
   }

   AddrDccIn.numSamples = AddrSurfInfoIn.numSamples = MAX2(1, config->info.samples);
   AddrSurfInfoIn.tileIndex = -1;

   if (!(surf->flags & RADEON_SURF_Z_OR_SBUFFER)) {
      AddrDccIn.numSamples = AddrSurfInfoIn.numFrags = MAX2(1, config->info.storage_samples);
   }

   /* Set the micro tile type. */
   if (surf->flags & RADEON_SURF_SCANOUT)
      AddrSurfInfoIn.tileType = ADDR_DISPLAYABLE;
   else if (surf->flags & RADEON_SURF_Z_OR_SBUFFER)
      AddrSurfInfoIn.tileType = ADDR_DEPTH_SAMPLE_ORDER;
   else
      AddrSurfInfoIn.tileType = ADDR_NON_DISPLAYABLE;

   AddrSurfInfoIn.flags.color = !(surf->flags & RADEON_SURF_Z_OR_SBUFFER);
   AddrSurfInfoIn.flags.depth = (surf->flags & RADEON_SURF_ZBUFFER) != 0;
   AddrSurfInfoIn.flags.cube = config->is_cube;
   AddrSurfInfoIn.flags.display = get_display_flag(config, surf);
   AddrSurfInfoIn.flags.pow2Pad = config->info.levels > 1;
   AddrSurfInfoIn.flags.tcCompatible = (surf->flags & RADEON_SURF_TC_COMPATIBLE_HTILE) != 0;

   /* Only degrade the tile mode for space if TC-compatible HTILE hasn't been
    * requested, because TC-compatible HTILE requires 2D tiling.
    */
   AddrSurfInfoIn.flags.opt4Space = !AddrSurfInfoIn.flags.tcCompatible &&
                                    !AddrSurfInfoIn.flags.fmask && config->info.samples <= 1 &&
                                    !(surf->flags & RADEON_SURF_FORCE_SWIZZLE_MODE);

   /* DCC notes:
    * - If we add MSAA support, keep in mind that CB can't decompress 8bpp
    *   with samples >= 4.
    * - Mipmapped array textures have low performance (discovered by a closed
    *   driver team).
    */
   AddrSurfInfoIn.flags.dccCompatible =
      info->chip_class >= GFX8 && info->has_graphics && /* disable DCC on compute-only chips */
      !(surf->flags & RADEON_SURF_Z_OR_SBUFFER) && !(surf->flags & RADEON_SURF_DISABLE_DCC) &&
      !compressed &&
      ((config->info.array_size == 1 && config->info.depth == 1) || config->info.levels == 1);

   AddrSurfInfoIn.flags.noStencil = (surf->flags & RADEON_SURF_SBUFFER) == 0;
   AddrSurfInfoIn.flags.compressZ = !!(surf->flags & RADEON_SURF_Z_OR_SBUFFER);

   /* On GFX7-GFX8, the DB uses the same pitch and tile mode (except tilesplit)
    * for Z and stencil. This can cause a number of problems which we work
    * around here:
    *
    * - a depth part that is incompatible with mipmapped texturing
    * - at least on Stoney, entirely incompatible Z/S aspects (e.g.
    *   incorrect tiling applied to the stencil part, stencil buffer
    *   memory accesses that go out of bounds) even without mipmapping
    *
    * Some piglit tests that are prone to different types of related
    * failures:
    *  ./bin/ext_framebuffer_multisample-upsample 2 stencil
    *  ./bin/framebuffer-blit-levels {draw,read} stencil
    *  ./bin/ext_framebuffer_multisample-unaligned-blit N {depth,stencil} {msaa,upsample,downsample}
    *  ./bin/fbo-depth-array fs-writes-{depth,stencil} / {depth,stencil}-{clear,layered-clear,draw}
    *  ./bin/depthstencil-render-miplevels 1024 d=s=z24_s8
    */
   int stencil_tile_idx = -1;

   if (AddrSurfInfoIn.flags.depth && !AddrSurfInfoIn.flags.noStencil &&
       (config->info.levels > 1 || info->family == CHIP_STONEY)) {
      /* Compute stencilTileIdx that is compatible with the (depth)
       * tileIdx. This degrades the depth surface if necessary to
       * ensure that a matching stencilTileIdx exists. */
      AddrSurfInfoIn.flags.matchStencilTileCfg = 1;

      /* Keep the depth mip-tail compatible with texturing. */
      AddrSurfInfoIn.flags.noStencil = 1;
   }

   /* Set preferred macrotile parameters. This is usually required
    * for shared resources. This is for 2D tiling only. */
   if (!(surf->flags & RADEON_SURF_Z_OR_SBUFFER) &&
       AddrSurfInfoIn.tileMode >= ADDR_TM_2D_TILED_THIN1 && surf->u.legacy.bankw &&
       surf->u.legacy.bankh && surf->u.legacy.mtilea && surf->u.legacy.tile_split) {
      /* If any of these parameters are incorrect, the calculation
       * will fail. */
      AddrTileInfoIn.banks = surf->u.legacy.num_banks;
      AddrTileInfoIn.bankWidth = surf->u.legacy.bankw;
      AddrTileInfoIn.bankHeight = surf->u.legacy.bankh;
      AddrTileInfoIn.macroAspectRatio = surf->u.legacy.mtilea;
      AddrTileInfoIn.tileSplitBytes = surf->u.legacy.tile_split;
      AddrTileInfoIn.pipeConfig = surf->u.legacy.pipe_config + 1; /* +1 compared to GB_TILE_MODE */
      AddrSurfInfoIn.flags.opt4Space = 0;
      AddrSurfInfoIn.pTileInfo = &AddrTileInfoIn;

      /* If AddrSurfInfoIn.pTileInfo is set, Addrlib doesn't set
       * the tile index, because we are expected to know it if
       * we know the other parameters.
       *
       * This is something that can easily be fixed in Addrlib.
       * For now, just figure it out here.
       * Note that only 2D_TILE_THIN1 is handled here.
       */
      assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
      assert(AddrSurfInfoIn.tileMode == ADDR_TM_2D_TILED_THIN1);

      if (info->chip_class == GFX6) {
         if (AddrSurfInfoIn.tileType == ADDR_DISPLAYABLE) {
            if (surf->bpe == 2)
               AddrSurfInfoIn.tileIndex = 11; /* 16bpp */
            else
               AddrSurfInfoIn.tileIndex = 12; /* 32bpp */
         } else {
            if (surf->bpe == 1)
               AddrSurfInfoIn.tileIndex = 14; /* 8bpp */
            else if (surf->bpe == 2)
               AddrSurfInfoIn.tileIndex = 15; /* 16bpp */
            else if (surf->bpe == 4)
               AddrSurfInfoIn.tileIndex = 16; /* 32bpp */
            else
               AddrSurfInfoIn.tileIndex = 17; /* 64bpp (and 128bpp) */
         }
      } else {
         /* GFX7 - GFX8 */
         if (AddrSurfInfoIn.tileType == ADDR_DISPLAYABLE)
            AddrSurfInfoIn.tileIndex = 10; /* 2D displayable */
         else
            AddrSurfInfoIn.tileIndex = 14; /* 2D non-displayable */

         /* Addrlib doesn't set this if tileIndex is forced like above. */
         AddrSurfInfoOut.macroModeIndex = cik_get_macro_tile_index(surf);
      }
   }

   surf->has_stencil = !!(surf->flags & RADEON_SURF_SBUFFER);
   surf->num_dcc_levels = 0;
   surf->surf_size = 0;
   surf->dcc_size = 0;
   surf->dcc_alignment = 1;
   surf->htile_size = 0;
   surf->htile_slice_size = 0;
   surf->htile_alignment = 1;

   const bool only_stencil =
      (surf->flags & RADEON_SURF_SBUFFER) && !(surf->flags & RADEON_SURF_ZBUFFER);

   /* Calculate texture layout information. */
   if (!only_stencil) {
      for (level = 0; level < config->info.levels; level++) {
         r = gfx6_compute_level(addrlib, config, surf, false, level, compressed, &AddrSurfInfoIn,
                                &AddrSurfInfoOut, &AddrDccIn, &AddrDccOut, &AddrHtileIn,
                                &AddrHtileOut);
         if (r)
            return r;

         if (level > 0)
            continue;

         if (!AddrSurfInfoOut.tcCompatible) {
            AddrSurfInfoIn.flags.tcCompatible = 0;
            surf->flags &= ~RADEON_SURF_TC_COMPATIBLE_HTILE;
         }

         if (AddrSurfInfoIn.flags.matchStencilTileCfg) {
            AddrSurfInfoIn.flags.matchStencilTileCfg = 0;
            AddrSurfInfoIn.tileIndex = AddrSurfInfoOut.tileIndex;
            stencil_tile_idx = AddrSurfInfoOut.stencilTileIdx;

            assert(stencil_tile_idx >= 0);
         }

         r = gfx6_surface_settings(addrlib, info, config, &AddrSurfInfoOut, surf);
         if (r)
            return r;
      }
   }

   /* Calculate texture layout information for stencil. */
   if (surf->flags & RADEON_SURF_SBUFFER) {
      AddrSurfInfoIn.tileIndex = stencil_tile_idx;
      AddrSurfInfoIn.bpp = 8;
      AddrSurfInfoIn.flags.depth = 0;
      AddrSurfInfoIn.flags.stencil = 1;
      AddrSurfInfoIn.flags.tcCompatible = 0;
      /* This will be ignored if AddrSurfInfoIn.pTileInfo is NULL. */
      AddrTileInfoIn.tileSplitBytes = surf->u.legacy.stencil_tile_split;

      for (level = 0; level < config->info.levels; level++) {
         r = gfx6_compute_level(addrlib, config, surf, true, level, compressed, &AddrSurfInfoIn,
                                &AddrSurfInfoOut, &AddrDccIn, &AddrDccOut, NULL, NULL);
         if (r)
            return r;

         /* DB uses the depth pitch for both stencil and depth. */
         if (!only_stencil) {
            if (surf->u.legacy.stencil_level[level].nblk_x != surf->u.legacy.level[level].nblk_x)
               surf->u.legacy.stencil_adjusted = true;
         } else {
            surf->u.legacy.level[level].nblk_x = surf->u.legacy.stencil_level[level].nblk_x;
         }

         if (level == 0) {
            if (only_stencil) {
               r = gfx6_surface_settings(addrlib, info, config, &AddrSurfInfoOut, surf);
               if (r)
                  return r;
            }

            /* For 2D modes only. */
            if (AddrSurfInfoOut.tileMode >= ADDR_TM_2D_TILED_THIN1) {
               surf->u.legacy.stencil_tile_split = AddrSurfInfoOut.pTileInfo->tileSplitBytes;
            }
         }
      }
   }

   /* Compute FMASK. */
   if (config->info.samples >= 2 && AddrSurfInfoIn.flags.color && info->has_graphics &&
       !(surf->flags & RADEON_SURF_NO_FMASK)) {
      ADDR_COMPUTE_FMASK_INFO_INPUT fin = {0};
      ADDR_COMPUTE_FMASK_INFO_OUTPUT fout = {0};
      ADDR_TILEINFO fmask_tile_info = {0};

      fin.size = sizeof(fin);
      fout.size = sizeof(fout);

      fin.tileMode = AddrSurfInfoOut.tileMode;
      fin.pitch = AddrSurfInfoOut.pitch;
      fin.height = config->info.height;
      fin.numSlices = AddrSurfInfoIn.numSlices;
      fin.numSamples = AddrSurfInfoIn.numSamples;
      fin.numFrags = AddrSurfInfoIn.numFrags;
      fin.tileIndex = -1;
      fout.pTileInfo = &fmask_tile_info;

      r = AddrComputeFmaskInfo(addrlib, &fin, &fout);
      if (r)
         return r;

      surf->fmask_size = fout.fmaskBytes;
      surf->fmask_alignment = fout.baseAlign;
      surf->fmask_tile_swizzle = 0;

      surf->u.legacy.fmask.slice_tile_max = (fout.pitch * fout.height) / 64;
      if (surf->u.legacy.fmask.slice_tile_max)
         surf->u.legacy.fmask.slice_tile_max -= 1;

      surf->u.legacy.fmask.tiling_index = fout.tileIndex;
      surf->u.legacy.fmask.bankh = fout.pTileInfo->bankHeight;
      surf->u.legacy.fmask.pitch_in_pixels = fout.pitch;
      surf->u.legacy.fmask.slice_size = fout.sliceSize;

      /* Compute tile swizzle for FMASK. */
      if (config->info.fmask_surf_index && !(surf->flags & RADEON_SURF_SHAREABLE)) {
         ADDR_COMPUTE_BASE_SWIZZLE_INPUT xin = {0};
         ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT xout = {0};

         xin.size = sizeof(ADDR_COMPUTE_BASE_SWIZZLE_INPUT);
         xout.size = sizeof(ADDR_COMPUTE_BASE_SWIZZLE_OUTPUT);

         /* This counter starts from 1 instead of 0. */
         xin.surfIndex = p_atomic_inc_return(config->info.fmask_surf_index);
         xin.tileIndex = fout.tileIndex;
         xin.macroModeIndex = fout.macroModeIndex;
         xin.pTileInfo = fout.pTileInfo;
         xin.tileMode = fin.tileMode;

         int r = AddrComputeBaseSwizzle(addrlib, &xin, &xout);
         if (r != ADDR_OK)
            return r;

         assert(xout.tileSwizzle <= u_bit_consecutive(0, sizeof(surf->tile_swizzle) * 8));
         surf->fmask_tile_swizzle = xout.tileSwizzle;
      }
   }

   /* Recalculate the whole DCC miptree size including disabled levels.
    * This is what addrlib does, but calling addrlib would be a lot more
    * complicated.
    */
   if (surf->dcc_size && config->info.levels > 1) {
      /* The smallest miplevels that are never compressed by DCC
       * still read the DCC buffer via TC if the base level uses DCC,
       * and for some reason the DCC buffer needs to be larger if
       * the miptree uses non-zero tile_swizzle. Otherwise there are
       * VM faults.
       *
       * "dcc_alignment * 4" was determined by trial and error.
       */
      surf->dcc_size = align64(surf->surf_size >> 8, surf->dcc_alignment * 4);
   }

   /* Make sure HTILE covers the whole miptree, because the shader reads
    * TC-compatible HTILE even for levels where it's disabled by DB.
    */
   if (surf->htile_size && config->info.levels > 1 &&
       surf->flags & RADEON_SURF_TC_COMPATIBLE_HTILE) {
      /* MSAA can't occur with levels > 1, so ignore the sample count. */
      const unsigned total_pixels = surf->surf_size / surf->bpe;
      const unsigned htile_block_size = 8 * 8;
      const unsigned htile_element_size = 4;

      surf->htile_size = (total_pixels / htile_block_size) * htile_element_size;
      surf->htile_size = align(surf->htile_size, surf->htile_alignment);
   } else if (!surf->htile_size) {
      /* Unset this if HTILE is not present. */
      surf->flags &= ~RADEON_SURF_TC_COMPATIBLE_HTILE;
   }

   surf->is_linear = surf->u.legacy.level[0].mode == RADEON_SURF_MODE_LINEAR_ALIGNED;
   surf->is_displayable = surf->is_linear || surf->micro_tile_mode == RADEON_MICRO_MODE_DISPLAY ||
                          surf->micro_tile_mode == RADEON_MICRO_MODE_RENDER;

   /* The rotated micro tile mode doesn't work if both CMASK and RB+ are
    * used at the same time. This case is not currently expected to occur
    * because we don't use rotated. Enforce this restriction on all chips
    * to facilitate testing.
    */
   if (surf->micro_tile_mode == RADEON_MICRO_MODE_RENDER) {
      assert(!"rotate micro tile mode is unsupported");
      return ADDR_ERROR;
   }

   ac_compute_cmask(info, config, surf);
   return 0;
}

/* This is only called when expecting a tiled layout. */
static int gfx9_get_preferred_swizzle_mode(ADDR_HANDLE addrlib, struct radeon_surf *surf,
                                           ADDR2_COMPUTE_SURFACE_INFO_INPUT *in, bool is_fmask,
                                           AddrSwizzleMode *swizzle_mode)
{
   ADDR_E_RETURNCODE ret;
   ADDR2_GET_PREFERRED_SURF_SETTING_INPUT sin = {0};
   ADDR2_GET_PREFERRED_SURF_SETTING_OUTPUT sout = {0};

   sin.size = sizeof(ADDR2_GET_PREFERRED_SURF_SETTING_INPUT);
   sout.size = sizeof(ADDR2_GET_PREFERRED_SURF_SETTING_OUTPUT);

   sin.flags = in->flags;
   sin.resourceType = in->resourceType;
   sin.format = in->format;
   sin.resourceLoction = ADDR_RSRC_LOC_INVIS;
   /* TODO: We could allow some of these: */
   sin.forbiddenBlock.micro = 1; /* don't allow the 256B swizzle modes */
   sin.forbiddenBlock.var = 1;   /* don't allow the variable-sized swizzle modes */
   sin.bpp = in->bpp;
   sin.width = in->width;
   sin.height = in->height;
   sin.numSlices = in->numSlices;
   sin.numMipLevels = in->numMipLevels;
   sin.numSamples = in->numSamples;
   sin.numFrags = in->numFrags;

   if (is_fmask) {
      sin.flags.display = 0;
      sin.flags.color = 0;
      sin.flags.fmask = 1;
   }

   if (surf->flags & RADEON_SURF_FORCE_MICRO_TILE_MODE) {
      sin.forbiddenBlock.linear = 1;

      if (surf->micro_tile_mode == RADEON_MICRO_MODE_DISPLAY)
         sin.preferredSwSet.sw_D = 1;
      else if (surf->micro_tile_mode == RADEON_MICRO_MODE_STANDARD)
         sin.preferredSwSet.sw_S = 1;
      else if (surf->micro_tile_mode == RADEON_MICRO_MODE_DEPTH)
         sin.preferredSwSet.sw_Z = 1;
      else if (surf->micro_tile_mode == RADEON_MICRO_MODE_RENDER)
         sin.preferredSwSet.sw_R = 1;
   }

   ret = Addr2GetPreferredSurfaceSetting(addrlib, &sin, &sout);
   if (ret != ADDR_OK)
      return ret;

   *swizzle_mode = sout.swizzleMode;
   return 0;
}

static bool is_dcc_supported_by_CB(const struct radeon_info *info, unsigned sw_mode)
{
   if (info->chip_class >= GFX10)
      return sw_mode == ADDR_SW_64KB_Z_X || sw_mode == ADDR_SW_64KB_R_X;

   return sw_mode != ADDR_SW_LINEAR;
}

ASSERTED static bool is_dcc_supported_by_L2(const struct radeon_info *info,
                                            const struct radeon_surf *surf)
{
   if (info->chip_class <= GFX9) {
      /* Only independent 64B blocks are supported. */
      return surf->u.gfx9.dcc.independent_64B_blocks && !surf->u.gfx9.dcc.independent_128B_blocks &&
             surf->u.gfx9.dcc.max_compressed_block_size == V_028C78_MAX_BLOCK_SIZE_64B;
   }

   if (info->family == CHIP_NAVI10) {
      /* Only independent 128B blocks are supported. */
      return !surf->u.gfx9.dcc.independent_64B_blocks && surf->u.gfx9.dcc.independent_128B_blocks &&
             surf->u.gfx9.dcc.max_compressed_block_size <= V_028C78_MAX_BLOCK_SIZE_128B;
   }

   if (info->family == CHIP_NAVI12 || info->family == CHIP_NAVI14) {
      /* Either 64B or 128B can be used, but not both.
       * If 64B is used, DCC image stores are unsupported.
       */
      return surf->u.gfx9.dcc.independent_64B_blocks != surf->u.gfx9.dcc.independent_128B_blocks &&
             (!surf->u.gfx9.dcc.independent_64B_blocks ||
              surf->u.gfx9.dcc.max_compressed_block_size == V_028C78_MAX_BLOCK_SIZE_64B) &&
             (!surf->u.gfx9.dcc.independent_128B_blocks ||
              surf->u.gfx9.dcc.max_compressed_block_size <= V_028C78_MAX_BLOCK_SIZE_128B);
   }

   /* 128B is recommended, but 64B can be set too if needed for 4K by DCN.
    * Since there is no reason to ever disable 128B, require it.
    * DCC image stores are always supported.
    */
   return surf->u.gfx9.dcc.independent_128B_blocks &&
          surf->u.gfx9.dcc.max_compressed_block_size <= V_028C78_MAX_BLOCK_SIZE_128B;
}

static bool is_dcc_supported_by_DCN(const struct radeon_info *info,
                                    const struct ac_surf_config *config,
                                    const struct radeon_surf *surf, bool rb_aligned,
                                    bool pipe_aligned)
{
   if (!info->use_display_dcc_unaligned && !info->use_display_dcc_with_retile_blit)
      return false;

   /* 16bpp and 64bpp are more complicated, so they are disallowed for now. */
   if (surf->bpe != 4)
      return false;

   /* Handle unaligned DCC. */
   if (info->use_display_dcc_unaligned && (rb_aligned || pipe_aligned))
      return false;

   switch (info->chip_class) {
   case GFX9:
      /* There are more constraints, but we always set
       * INDEPENDENT_64B_BLOCKS = 1 and MAX_COMPRESSED_BLOCK_SIZE = 64B,
       * which always works.
       */
      assert(surf->u.gfx9.dcc.independent_64B_blocks &&
             surf->u.gfx9.dcc.max_compressed_block_size == V_028C78_MAX_BLOCK_SIZE_64B);
      return true;
   case GFX10:
   case GFX10_3:
      /* DCN requires INDEPENDENT_128B_BLOCKS = 0 only on Navi1x. */
      if (info->chip_class == GFX10 && surf->u.gfx9.dcc.independent_128B_blocks)
         return false;

      /* For 4K, DCN requires INDEPENDENT_64B_BLOCKS = 1. */
      return ((config->info.width <= 2560 && config->info.height <= 2560) ||
              (surf->u.gfx9.dcc.independent_64B_blocks &&
               surf->u.gfx9.dcc.max_compressed_block_size == V_028C78_MAX_BLOCK_SIZE_64B));
   default:
      unreachable("unhandled chip");
      return false;
   }
}

static int gfx9_compute_miptree(struct ac_addrlib *addrlib, const struct radeon_info *info,
                                const struct ac_surf_config *config, struct radeon_surf *surf,
                                bool compressed, ADDR2_COMPUTE_SURFACE_INFO_INPUT *in)
{
   ADDR2_MIP_INFO mip_info[RADEON_SURF_MAX_LEVELS] = {0};
   ADDR2_COMPUTE_SURFACE_INFO_OUTPUT out = {0};
   ADDR_E_RETURNCODE ret;

   out.size = sizeof(ADDR2_COMPUTE_SURFACE_INFO_OUTPUT);
   out.pMipInfo = mip_info;

   ret = Addr2ComputeSurfaceInfo(addrlib->handle, in, &out);
   if (ret != ADDR_OK)
      return ret;

   if (in->flags.stencil) {
      surf->u.gfx9.stencil.swizzle_mode = in->swizzleMode;
      surf->u.gfx9.stencil.epitch =
         out.epitchIsHeight ? out.mipChainHeight - 1 : out.mipChainPitch - 1;
      surf->surf_alignment = MAX2(surf->surf_alignment, out.baseAlign);
      surf->u.gfx9.stencil_offset = align(surf->surf_size, out.baseAlign);
      surf->surf_size = surf->u.gfx9.stencil_offset + out.surfSize;
      return 0;
   }

   surf->u.gfx9.surf.swizzle_mode = in->swizzleMode;
   surf->u.gfx9.surf.epitch = out.epitchIsHeight ? out.mipChainHeight - 1 : out.mipChainPitch - 1;

   /* CMASK fast clear uses these even if FMASK isn't allocated.
    * FMASK only supports the Z swizzle modes, whose numbers are multiples of 4.
    */
   surf->u.gfx9.fmask.swizzle_mode = surf->u.gfx9.surf.swizzle_mode & ~0x3;
   surf->u.gfx9.fmask.epitch = surf->u.gfx9.surf.epitch;

   surf->u.gfx9.surf_slice_size = out.sliceSize;
   surf->u.gfx9.surf_pitch = out.pitch;
   surf->u.gfx9.surf_height = out.height;
   surf->surf_size = out.surfSize;
   surf->surf_alignment = out.baseAlign;

   if (!compressed && surf->blk_w > 1 && out.pitch == out.pixelPitch &&
       surf->u.gfx9.surf.swizzle_mode == ADDR_SW_LINEAR) {
      /* Adjust surf_pitch to be in elements units not in pixels */
      surf->u.gfx9.surf_pitch = align(surf->u.gfx9.surf_pitch / surf->blk_w, 256 / surf->bpe);
      surf->u.gfx9.surf.epitch =
         MAX2(surf->u.gfx9.surf.epitch, surf->u.gfx9.surf_pitch * surf->blk_w - 1);
      /* The surface is really a surf->bpe bytes per pixel surface even if we
       * use it as a surf->bpe bytes per element one.
       * Adjust surf_slice_size and surf_size to reflect the change
       * made to surf_pitch.
       */
      surf->u.gfx9.surf_slice_size =
         MAX2(surf->u.gfx9.surf_slice_size,
              surf->u.gfx9.surf_pitch * out.height * surf->bpe * surf->blk_w);
      surf->surf_size = surf->u.gfx9.surf_slice_size * in->numSlices;
   }

   if (in->swizzleMode == ADDR_SW_LINEAR) {
      for (unsigned i = 0; i < in->numMipLevels; i++) {
         surf->u.gfx9.offset[i] = mip_info[i].offset;
         surf->u.gfx9.pitch[i] = mip_info[i].pitch;
      }
   }

   surf->u.gfx9.base_mip_width = mip_info[0].pitch;
   surf->u.gfx9.base_mip_height = mip_info[0].height;

   if (in->flags.depth) {
      assert(in->swizzleMode != ADDR_SW_LINEAR);

      if (surf->flags & RADEON_SURF_NO_HTILE)
         return 0;

      /* HTILE */
      ADDR2_COMPUTE_HTILE_INFO_INPUT hin = {0};
      ADDR2_COMPUTE_HTILE_INFO_OUTPUT hout = {0};

      hin.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_INPUT);
      hout.size = sizeof(ADDR2_COMPUTE_HTILE_INFO_OUTPUT);

      assert(in->flags.metaPipeUnaligned == 0);
      assert(in->flags.metaRbUnaligned == 0);

      hin.hTileFlags.pipeAligned = 1;
      hin.hTileFlags.rbAligned = 1;
      hin.depthFlags = in->flags;
      hin.swizzleMode = in->swizzleMode;
      hin.unalignedWidth = in->width;
      hin.unalignedHeight = in->height;
      hin.numSlices = in->numSlices;
      hin.numMipLevels = in->numMipLevels;
      hin.firstMipIdInTail = out.firstMipIdInTail;

      ret = Addr2ComputeHtileInfo(addrlib->handle, &hin, &hout);
      if (ret != ADDR_OK)
         return ret;

      surf->htile_size = hout.htileBytes;
      surf->htile_slice_size = hout.sliceSize;
      surf->htile_alignment = hout.baseAlign;
      return 0;
   }

   {
      /* Compute tile swizzle for the color surface.
       * All *_X and *_T modes can use the swizzle.
       */
      if (config->info.surf_index && in->swizzleMode >= ADDR_SW_64KB_Z_T && !out.mipChainInTail &&
          !(surf->flags & RADEON_SURF_SHAREABLE) && !in->flags.display) {
         ADDR2_COMPUTE_PIPEBANKXOR_INPUT xin = {0};
         ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT xout = {0};

         xin.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_INPUT);
         xout.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT);

         xin.surfIndex = p_atomic_inc_return(config->info.surf_index) - 1;
         xin.flags = in->flags;
         xin.swizzleMode = in->swizzleMode;
         xin.resourceType = in->resourceType;
         xin.format = in->format;
         xin.numSamples = in->numSamples;
         xin.numFrags = in->numFrags;

         ret = Addr2ComputePipeBankXor(addrlib->handle, &xin, &xout);
         if (ret != ADDR_OK)
            return ret;

         assert(xout.pipeBankXor <= u_bit_consecutive(0, sizeof(surf->tile_swizzle) * 8));
         surf->tile_swizzle = xout.pipeBankXor;
      }

      /* DCC */
      if (info->has_graphics && !(surf->flags & RADEON_SURF_DISABLE_DCC) && !compressed &&
          is_dcc_supported_by_CB(info, in->swizzleMode) &&
          (!in->flags.display ||
           is_dcc_supported_by_DCN(info, config, surf, !in->flags.metaRbUnaligned,
                                   !in->flags.metaPipeUnaligned))) {
         ADDR2_COMPUTE_DCCINFO_INPUT din = {0};
         ADDR2_COMPUTE_DCCINFO_OUTPUT dout = {0};
         ADDR2_META_MIP_INFO meta_mip_info[RADEON_SURF_MAX_LEVELS] = {0};

         din.size = sizeof(ADDR2_COMPUTE_DCCINFO_INPUT);
         dout.size = sizeof(ADDR2_COMPUTE_DCCINFO_OUTPUT);
         dout.pMipInfo = meta_mip_info;

         din.dccKeyFlags.pipeAligned = !in->flags.metaPipeUnaligned;
         din.dccKeyFlags.rbAligned = !in->flags.metaRbUnaligned;
         din.resourceType = in->resourceType;
         din.swizzleMode = in->swizzleMode;
         din.bpp = in->bpp;
         din.unalignedWidth = in->width;
         din.unalignedHeight = in->height;
         din.numSlices = in->numSlices;
         din.numFrags = in->numFrags;
         din.numMipLevels = in->numMipLevels;
         din.dataSurfaceSize = out.surfSize;
         din.firstMipIdInTail = out.firstMipIdInTail;

         ret = Addr2ComputeDccInfo(addrlib->handle, &din, &dout);
         if (ret != ADDR_OK)
            return ret;

         surf->u.gfx9.dcc.rb_aligned = din.dccKeyFlags.rbAligned;
         surf->u.gfx9.dcc.pipe_aligned = din.dccKeyFlags.pipeAligned;
         surf->u.gfx9.dcc_block_width = dout.compressBlkWidth;
         surf->u.gfx9.dcc_block_height = dout.compressBlkHeight;
         surf->u.gfx9.dcc_block_depth = dout.compressBlkDepth;
         surf->dcc_size = dout.dccRamSize;
         surf->dcc_alignment = dout.dccRamBaseAlign;
         surf->num_dcc_levels = in->numMipLevels;

         /* Disable DCC for levels that are in the mip tail.
          *
          * There are two issues that this is intended to
          * address:
          *
          * 1. Multiple mip levels may share a cache line. This
          *    can lead to corruption when switching between
          *    rendering to different mip levels because the
          *    RBs don't maintain coherency.
          *
          * 2. Texturing with metadata after rendering sometimes
          *    fails with corruption, probably for a similar
          *    reason.
          *
          * Working around these issues for all levels in the
          * mip tail may be overly conservative, but it's what
          * Vulkan does.
          *
          * Alternative solutions that also work but are worse:
          * - Disable DCC entirely.
          * - Flush TC L2 after rendering.
          */
         for (unsigned i = 0; i < in->numMipLevels; i++) {
            if (meta_mip_info[i].inMiptail) {
               /* GFX10 can only compress the first level
                * in the mip tail.
                *
                * TODO: Try to do the same thing for gfx9
                *       if there are no regressions.
                */
               if (info->chip_class >= GFX10)
                  surf->num_dcc_levels = i + 1;
               else
                  surf->num_dcc_levels = i;
               break;
            }
         }

         if (!surf->num_dcc_levels)
            surf->dcc_size = 0;

         surf->u.gfx9.display_dcc_size = surf->dcc_size;
         surf->u.gfx9.display_dcc_alignment = surf->dcc_alignment;
         surf->u.gfx9.display_dcc_pitch_max = dout.pitch - 1;
         surf->u.gfx9.dcc_pitch_max = dout.pitch - 1;

         /* Compute displayable DCC. */
         if (in->flags.display && surf->num_dcc_levels && info->use_display_dcc_with_retile_blit) {
            /* Compute displayable DCC info. */
            din.dccKeyFlags.pipeAligned = 0;
            din.dccKeyFlags.rbAligned = 0;

            assert(din.numSlices == 1);
            assert(din.numMipLevels == 1);
            assert(din.numFrags == 1);
            assert(surf->tile_swizzle == 0);
            assert(surf->u.gfx9.dcc.pipe_aligned || surf->u.gfx9.dcc.rb_aligned);

            ret = Addr2ComputeDccInfo(addrlib->handle, &din, &dout);
            if (ret != ADDR_OK)
               return ret;

            surf->u.gfx9.display_dcc_size = dout.dccRamSize;
            surf->u.gfx9.display_dcc_alignment = dout.dccRamBaseAlign;
            surf->u.gfx9.display_dcc_pitch_max = dout.pitch - 1;
            assert(surf->u.gfx9.display_dcc_size <= surf->dcc_size);

            surf->u.gfx9.dcc_retile_use_uint16 =
               surf->u.gfx9.display_dcc_size <= UINT16_MAX + 1 && surf->dcc_size <= UINT16_MAX + 1;

            /* Align the retile map size to get more hash table hits and
             * decrease the maximum memory footprint when all retile maps
             * are cached in the hash table.
             */
            unsigned retile_dim[2] = {in->width, in->height};

            for (unsigned i = 0; i < 2; i++) {
               /* Increase the alignment as the size increases.
                * Greater alignment increases retile compute work,
                * but decreases maximum memory footprint for the cache.
                *
                * With this alignment, the worst case memory footprint of
                * the cache is:
                *   1920x1080: 55 MB
                *   2560x1440: 99 MB
                *   3840x2160: 305 MB
                *
                * The worst case size in MB can be computed in Haskell as follows:
                *   (sum (map get_retile_size (map get_dcc_size (deduplicate (map align_pair
                *       [(i*16,j*16) | i <- [1..maxwidth`div`16], j <- [1..maxheight`div`16]])))))
                * `div` 1024^2 where alignment x = if x <= 512 then 16 else if x <= 1024 then 32
                * else if x <= 2048 then 64 else 128 align x = (x + (alignment x) - 1) `div`
                * (alignment x) * (alignment x) align_pair e = (align (fst e), align (snd e))
                *       deduplicate = map head . groupBy (\ a b -> ((fst a) == (fst b)) && ((snd a)
                * == (snd b))) . sortBy compare get_dcc_size e = ((fst e) * (snd e) * bpp) `div` 256
                *       get_retile_size dcc_size = dcc_size * 2 * (if dcc_size <= 2^16 then 2 else
                * 4) bpp = 4; maxwidth = 3840; maxheight = 2160
                */
               if (retile_dim[i] <= 512)
                  retile_dim[i] = align(retile_dim[i], 16);
               else if (retile_dim[i] <= 1024)
                  retile_dim[i] = align(retile_dim[i], 32);
               else if (retile_dim[i] <= 2048)
                  retile_dim[i] = align(retile_dim[i], 64);
               else
                  retile_dim[i] = align(retile_dim[i], 128);

               /* Don't align more than the DCC pixel alignment. */
               assert(dout.metaBlkWidth >= 128 && dout.metaBlkHeight >= 128);
            }

            surf->u.gfx9.dcc_retile_num_elements =
               DIV_ROUND_UP(retile_dim[0], dout.compressBlkWidth) *
               DIV_ROUND_UP(retile_dim[1], dout.compressBlkHeight) * 2;
            /* Align the size to 4 (for the compute shader). */
            surf->u.gfx9.dcc_retile_num_elements = align(surf->u.gfx9.dcc_retile_num_elements, 4);

            /* Compute address mapping from non-displayable to displayable DCC. */
            ADDR2_COMPUTE_DCC_ADDRFROMCOORD_INPUT addrin;
            memset(&addrin, 0, sizeof(addrin));
            addrin.size = sizeof(addrin);
            addrin.swizzleMode = din.swizzleMode;
            addrin.resourceType = din.resourceType;
            addrin.bpp = din.bpp;
            addrin.numSlices = 1;
            addrin.numMipLevels = 1;
            addrin.numFrags = 1;
            addrin.pitch = dout.pitch;
            addrin.height = dout.height;
            addrin.compressBlkWidth = dout.compressBlkWidth;
            addrin.compressBlkHeight = dout.compressBlkHeight;
            addrin.compressBlkDepth = dout.compressBlkDepth;
            addrin.metaBlkWidth = dout.metaBlkWidth;
            addrin.metaBlkHeight = dout.metaBlkHeight;
            addrin.metaBlkDepth = dout.metaBlkDepth;
            addrin.dccRamSliceSize = 0; /* Don't care for non-layered images. */

            surf->u.gfx9.dcc_retile_map = ac_compute_dcc_retile_map(
               addrlib, info, retile_dim[0], retile_dim[1], surf->u.gfx9.dcc.rb_aligned,
               surf->u.gfx9.dcc.pipe_aligned, surf->u.gfx9.dcc_retile_use_uint16,
               surf->u.gfx9.dcc_retile_num_elements, &addrin);
            if (!surf->u.gfx9.dcc_retile_map)
               return ADDR_OUTOFMEMORY;
         }
      }

      /* FMASK */
      if (in->numSamples > 1 && info->has_graphics && !(surf->flags & RADEON_SURF_NO_FMASK)) {
         ADDR2_COMPUTE_FMASK_INFO_INPUT fin = {0};
         ADDR2_COMPUTE_FMASK_INFO_OUTPUT fout = {0};

         fin.size = sizeof(ADDR2_COMPUTE_FMASK_INFO_INPUT);
         fout.size = sizeof(ADDR2_COMPUTE_FMASK_INFO_OUTPUT);

         ret = gfx9_get_preferred_swizzle_mode(addrlib->handle, surf, in, true, &fin.swizzleMode);
         if (ret != ADDR_OK)
            return ret;

         fin.unalignedWidth = in->width;
         fin.unalignedHeight = in->height;
         fin.numSlices = in->numSlices;
         fin.numSamples = in->numSamples;
         fin.numFrags = in->numFrags;

         ret = Addr2ComputeFmaskInfo(addrlib->handle, &fin, &fout);
         if (ret != ADDR_OK)
            return ret;

         surf->u.gfx9.fmask.swizzle_mode = fin.swizzleMode;
         surf->u.gfx9.fmask.epitch = fout.pitch - 1;
         surf->fmask_size = fout.fmaskBytes;
         surf->fmask_alignment = fout.baseAlign;

         /* Compute tile swizzle for the FMASK surface. */
         if (config->info.fmask_surf_index && fin.swizzleMode >= ADDR_SW_64KB_Z_T &&
             !(surf->flags & RADEON_SURF_SHAREABLE)) {
            ADDR2_COMPUTE_PIPEBANKXOR_INPUT xin = {0};
            ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT xout = {0};

            xin.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_INPUT);
            xout.size = sizeof(ADDR2_COMPUTE_PIPEBANKXOR_OUTPUT);

            /* This counter starts from 1 instead of 0. */
            xin.surfIndex = p_atomic_inc_return(config->info.fmask_surf_index);
            xin.flags = in->flags;
            xin.swizzleMode = fin.swizzleMode;
            xin.resourceType = in->resourceType;
            xin.format = in->format;
            xin.numSamples = in->numSamples;
            xin.numFrags = in->numFrags;

            ret = Addr2ComputePipeBankXor(addrlib->handle, &xin, &xout);
            if (ret != ADDR_OK)
               return ret;

            assert(xout.pipeBankXor <= u_bit_consecutive(0, sizeof(surf->fmask_tile_swizzle) * 8));
            surf->fmask_tile_swizzle = xout.pipeBankXor;
         }
      }

      /* CMASK -- on GFX10 only for FMASK */
      if (in->swizzleMode != ADDR_SW_LINEAR && in->resourceType == ADDR_RSRC_TEX_2D &&
          ((info->chip_class <= GFX9 && in->numSamples == 1 && in->flags.metaPipeUnaligned == 0 &&
            in->flags.metaRbUnaligned == 0) ||
           (surf->fmask_size && in->numSamples >= 2))) {
         ADDR2_COMPUTE_CMASK_INFO_INPUT cin = {0};
         ADDR2_COMPUTE_CMASK_INFO_OUTPUT cout = {0};

         cin.size = sizeof(ADDR2_COMPUTE_CMASK_INFO_INPUT);
         cout.size = sizeof(ADDR2_COMPUTE_CMASK_INFO_OUTPUT);

         assert(in->flags.metaPipeUnaligned == 0);
         assert(in->flags.metaRbUnaligned == 0);

         cin.cMaskFlags.pipeAligned = 1;
         cin.cMaskFlags.rbAligned = 1;
         cin.resourceType = in->resourceType;
         cin.unalignedWidth = in->width;
         cin.unalignedHeight = in->height;
         cin.numSlices = in->numSlices;

         if (in->numSamples > 1)
            cin.swizzleMode = surf->u.gfx9.fmask.swizzle_mode;
         else
            cin.swizzleMode = in->swizzleMode;

         ret = Addr2ComputeCmaskInfo(addrlib->handle, &cin, &cout);
         if (ret != ADDR_OK)
            return ret;

         surf->cmask_size = cout.cmaskBytes;
         surf->cmask_alignment = cout.baseAlign;
      }
   }

   return 0;
}

static int gfx9_compute_surface(struct ac_addrlib *addrlib, const struct radeon_info *info,
                                const struct ac_surf_config *config, enum radeon_surf_mode mode,
                                struct radeon_surf *surf)
{
   bool compressed;
   ADDR2_COMPUTE_SURFACE_INFO_INPUT AddrSurfInfoIn = {0};
   int r;

   AddrSurfInfoIn.size = sizeof(ADDR2_COMPUTE_SURFACE_INFO_INPUT);

   compressed = surf->blk_w == 4 && surf->blk_h == 4;

   /* The format must be set correctly for the allocation of compressed
    * textures to work. In other cases, setting the bpp is sufficient. */
   if (compressed) {
      switch (surf->bpe) {
      case 8:
         AddrSurfInfoIn.format = ADDR_FMT_BC1;
         break;
      case 16:
         AddrSurfInfoIn.format = ADDR_FMT_BC3;
         break;
      default:
         assert(0);
      }
   } else {
      switch (surf->bpe) {
      case 1:
         assert(!(surf->flags & RADEON_SURF_ZBUFFER));
         AddrSurfInfoIn.format = ADDR_FMT_8;
         break;
      case 2:
         assert(surf->flags & RADEON_SURF_ZBUFFER || !(surf->flags & RADEON_SURF_SBUFFER));
         AddrSurfInfoIn.format = ADDR_FMT_16;
         break;
      case 4:
         assert(surf->flags & RADEON_SURF_ZBUFFER || !(surf->flags & RADEON_SURF_SBUFFER));
         AddrSurfInfoIn.format = ADDR_FMT_32;
         break;
      case 8:
         assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
         AddrSurfInfoIn.format = ADDR_FMT_32_32;
         break;
      case 12:
         assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
         AddrSurfInfoIn.format = ADDR_FMT_32_32_32;
         break;
      case 16:
         assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
         AddrSurfInfoIn.format = ADDR_FMT_32_32_32_32;
         break;
      default:
         assert(0);
      }
      AddrSurfInfoIn.bpp = surf->bpe * 8;
   }

   bool is_color_surface = !(surf->flags & RADEON_SURF_Z_OR_SBUFFER);
   AddrSurfInfoIn.flags.color = is_color_surface && !(surf->flags & RADEON_SURF_NO_RENDER_TARGET);
   AddrSurfInfoIn.flags.depth = (surf->flags & RADEON_SURF_ZBUFFER) != 0;
   AddrSurfInfoIn.flags.display = get_display_flag(config, surf);
   /* flags.texture currently refers to TC-compatible HTILE */
   AddrSurfInfoIn.flags.texture = is_color_surface || surf->flags & RADEON_SURF_TC_COMPATIBLE_HTILE;
   AddrSurfInfoIn.flags.opt4space = 1;

   AddrSurfInfoIn.numMipLevels = config->info.levels;
   AddrSurfInfoIn.numSamples = MAX2(1, config->info.samples);
   AddrSurfInfoIn.numFrags = AddrSurfInfoIn.numSamples;

   if (!(surf->flags & RADEON_SURF_Z_OR_SBUFFER))
      AddrSurfInfoIn.numFrags = MAX2(1, config->info.storage_samples);

   /* GFX9 doesn't support 1D depth textures, so allocate all 1D textures
    * as 2D to avoid having shader variants for 1D vs 2D, so all shaders
    * must sample 1D textures as 2D. */
   if (config->is_3d)
      AddrSurfInfoIn.resourceType = ADDR_RSRC_TEX_3D;
   else if (info->chip_class != GFX9 && config->is_1d)
      AddrSurfInfoIn.resourceType = ADDR_RSRC_TEX_1D;
   else
      AddrSurfInfoIn.resourceType = ADDR_RSRC_TEX_2D;

   AddrSurfInfoIn.width = config->info.width;
   AddrSurfInfoIn.height = config->info.height;

   if (config->is_3d)
      AddrSurfInfoIn.numSlices = config->info.depth;
   else if (config->is_cube)
      AddrSurfInfoIn.numSlices = 6;
   else
      AddrSurfInfoIn.numSlices = config->info.array_size;

   /* This is propagated to DCC. It must be 0 for HTILE and CMASK. */
   AddrSurfInfoIn.flags.metaPipeUnaligned = 0;
   AddrSurfInfoIn.flags.metaRbUnaligned = 0;

   /* Optimal values for the L2 cache. */
   if (info->chip_class == GFX9) {
      surf->u.gfx9.dcc.independent_64B_blocks = 1;
      surf->u.gfx9.dcc.independent_128B_blocks = 0;
      surf->u.gfx9.dcc.max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
   } else if (info->chip_class >= GFX10) {
      surf->u.gfx9.dcc.independent_64B_blocks = 0;
      surf->u.gfx9.dcc.independent_128B_blocks = 1;
      surf->u.gfx9.dcc.max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_128B;
   }

   if (AddrSurfInfoIn.flags.display) {
      /* The display hardware can only read DCC with RB_ALIGNED=0 and
       * PIPE_ALIGNED=0. PIPE_ALIGNED really means L2CACHE_ALIGNED.
       *
       * The CB block requires RB_ALIGNED=1 except 1 RB chips.
       * PIPE_ALIGNED is optional, but PIPE_ALIGNED=0 requires L2 flushes
       * after rendering, so PIPE_ALIGNED=1 is recommended.
       */
      if (info->use_display_dcc_unaligned) {
         AddrSurfInfoIn.flags.metaPipeUnaligned = 1;
         AddrSurfInfoIn.flags.metaRbUnaligned = 1;
      }

      /* Adjust DCC settings to meet DCN requirements. */
      if (info->use_display_dcc_unaligned || info->use_display_dcc_with_retile_blit) {
         /* Only Navi12/14 support independent 64B blocks in L2,
          * but without DCC image stores.
          */
         if (info->family == CHIP_NAVI12 || info->family == CHIP_NAVI14) {
            surf->u.gfx9.dcc.independent_64B_blocks = 1;
            surf->u.gfx9.dcc.independent_128B_blocks = 0;
            surf->u.gfx9.dcc.max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
         }

         if (info->chip_class >= GFX10_3) {
            surf->u.gfx9.dcc.independent_64B_blocks = 1;
            surf->u.gfx9.dcc.independent_128B_blocks = 1;
            surf->u.gfx9.dcc.max_compressed_block_size = V_028C78_MAX_BLOCK_SIZE_64B;
         }
      }
   }

   switch (mode) {
   case RADEON_SURF_MODE_LINEAR_ALIGNED:
      assert(config->info.samples <= 1);
      assert(!(surf->flags & RADEON_SURF_Z_OR_SBUFFER));
      AddrSurfInfoIn.swizzleMode = ADDR_SW_LINEAR;
      break;

   case RADEON_SURF_MODE_1D:
   case RADEON_SURF_MODE_2D:
      if (surf->flags & RADEON_SURF_IMPORTED ||
          (info->chip_class >= GFX10 && surf->flags & RADEON_SURF_FORCE_SWIZZLE_MODE)) {
         AddrSurfInfoIn.swizzleMode = surf->u.gfx9.surf.swizzle_mode;
         break;
      }

      r = gfx9_get_preferred_swizzle_mode(addrlib->handle, surf, &AddrSurfInfoIn, false,
                                          &AddrSurfInfoIn.swizzleMode);
      if (r)
         return r;
      break;

   default:
      assert(0);
   }

   surf->u.gfx9.resource_type = AddrSurfInfoIn.resourceType;
   surf->has_stencil = !!(surf->flags & RADEON_SURF_SBUFFER);

   surf->num_dcc_levels = 0;
   surf->surf_size = 0;
   surf->fmask_size = 0;
   surf->dcc_size = 0;
   surf->htile_size = 0;
   surf->htile_slice_size = 0;
   surf->u.gfx9.surf_offset = 0;
   surf->u.gfx9.stencil_offset = 0;
   surf->cmask_size = 0;
   surf->u.gfx9.dcc_retile_use_uint16 = false;
   surf->u.gfx9.dcc_retile_num_elements = 0;
   surf->u.gfx9.dcc_retile_map = NULL;

   /* Calculate texture layout information. */
   r = gfx9_compute_miptree(addrlib, info, config, surf, compressed, &AddrSurfInfoIn);
   if (r)
      return r;

   /* Calculate texture layout information for stencil. */
   if (surf->flags & RADEON_SURF_SBUFFER) {
      AddrSurfInfoIn.flags.stencil = 1;
      AddrSurfInfoIn.bpp = 8;
      AddrSurfInfoIn.format = ADDR_FMT_8;

      if (!AddrSurfInfoIn.flags.depth) {
         r = gfx9_get_preferred_swizzle_mode(addrlib->handle, surf, &AddrSurfInfoIn, false,
                                             &AddrSurfInfoIn.swizzleMode);
         if (r)
            return r;
      } else
         AddrSurfInfoIn.flags.depth = 0;

      r = gfx9_compute_miptree(addrlib, info, config, surf, compressed, &AddrSurfInfoIn);
      if (r)
         return r;
   }

   surf->is_linear = surf->u.gfx9.surf.swizzle_mode == ADDR_SW_LINEAR;

   /* Query whether the surface is displayable. */
   /* This is only useful for surfaces that are allocated without SCANOUT. */
   bool displayable = false;
   if (!config->is_3d && !config->is_cube) {
      r = Addr2IsValidDisplaySwizzleMode(addrlib->handle, surf->u.gfx9.surf.swizzle_mode,
                                         surf->bpe * 8, &displayable);
      if (r)
         return r;

      /* Display needs unaligned DCC. */
      if (surf->num_dcc_levels &&
          (!is_dcc_supported_by_DCN(info, config, surf, surf->u.gfx9.dcc.rb_aligned,
                                    surf->u.gfx9.dcc.pipe_aligned) ||
           /* Don't set is_displayable if displayable DCC is missing. */
           (info->use_display_dcc_with_retile_blit && !surf->u.gfx9.dcc_retile_num_elements)))
         displayable = false;
   }
   surf->is_displayable = displayable;

   /* Validate that we allocated a displayable surface if requested. */
   assert(!AddrSurfInfoIn.flags.display || surf->is_displayable);

   /* Validate that DCC is set up correctly. */
   if (surf->num_dcc_levels) {
      assert(is_dcc_supported_by_L2(info, surf));
      if (AddrSurfInfoIn.flags.color)
         assert(is_dcc_supported_by_CB(info, surf->u.gfx9.surf.swizzle_mode));
      if (AddrSurfInfoIn.flags.display) {
         assert(is_dcc_supported_by_DCN(info, config, surf, surf->u.gfx9.dcc.rb_aligned,
                                        surf->u.gfx9.dcc.pipe_aligned));
      }
   }

   if (info->has_graphics && !compressed && !config->is_3d && config->info.levels == 1 &&
       AddrSurfInfoIn.flags.color && !surf->is_linear &&
       surf->surf_alignment >= 64 * 1024 && /* 64KB tiling */
       !(surf->flags & (RADEON_SURF_DISABLE_DCC | RADEON_SURF_FORCE_SWIZZLE_MODE |
                        RADEON_SURF_FORCE_MICRO_TILE_MODE))) {
      /* Validate that DCC is enabled if DCN can do it. */
      if ((info->use_display_dcc_unaligned || info->use_display_dcc_with_retile_blit) &&
          AddrSurfInfoIn.flags.display && surf->bpe == 4) {
         assert(surf->num_dcc_levels);
      }

      /* Validate that non-scanout DCC is always enabled. */
      if (!AddrSurfInfoIn.flags.display)
         assert(surf->num_dcc_levels);
   }

   if (!surf->htile_size) {
      /* Unset this if HTILE is not present. */
      surf->flags &= ~RADEON_SURF_TC_COMPATIBLE_HTILE;
   }

   switch (surf->u.gfx9.surf.swizzle_mode) {
   /* S = standard. */
   case ADDR_SW_256B_S:
   case ADDR_SW_4KB_S:
   case ADDR_SW_64KB_S:
   case ADDR_SW_64KB_S_T:
   case ADDR_SW_4KB_S_X:
   case ADDR_SW_64KB_S_X:
      surf->micro_tile_mode = RADEON_MICRO_MODE_STANDARD;
      break;

   /* D = display. */
   case ADDR_SW_LINEAR:
   case ADDR_SW_256B_D:
   case ADDR_SW_4KB_D:
   case ADDR_SW_64KB_D:
   case ADDR_SW_64KB_D_T:
   case ADDR_SW_4KB_D_X:
   case ADDR_SW_64KB_D_X:
      surf->micro_tile_mode = RADEON_MICRO_MODE_DISPLAY;
      break;

   /* R = rotated (gfx9), render target (gfx10). */
   case ADDR_SW_256B_R:
   case ADDR_SW_4KB_R:
   case ADDR_SW_64KB_R:
   case ADDR_SW_64KB_R_T:
   case ADDR_SW_4KB_R_X:
   case ADDR_SW_64KB_R_X:
   case ADDR_SW_VAR_R_X:
      /* The rotated micro tile mode doesn't work if both CMASK and RB+ are
       * used at the same time. We currently do not use rotated
       * in gfx9.
       */
      assert(info->chip_class >= GFX10 || !"rotate micro tile mode is unsupported");
      surf->micro_tile_mode = RADEON_MICRO_MODE_RENDER;
      break;

   /* Z = depth. */
   case ADDR_SW_4KB_Z:
   case ADDR_SW_64KB_Z:
   case ADDR_SW_64KB_Z_T:
   case ADDR_SW_4KB_Z_X:
   case ADDR_SW_64KB_Z_X:
   case ADDR_SW_VAR_Z_X:
      surf->micro_tile_mode = RADEON_MICRO_MODE_DEPTH;
      break;

   default:
      assert(0);
   }

   return 0;
}

int ac_compute_surface(struct ac_addrlib *addrlib, const struct radeon_info *info,
                       const struct ac_surf_config *config, enum radeon_surf_mode mode,
                       struct radeon_surf *surf)
{
   int r;

   r = surf_config_sanity(config, surf->flags);
   if (r)
      return r;

   if (info->chip_class >= GFX9)
      r = gfx9_compute_surface(addrlib, info, config, mode, surf);
   else
      r = gfx6_compute_surface(addrlib->handle, info, config, mode, surf);

   if (r)
      return r;

   /* Determine the memory layout of multiple allocations in one buffer. */
   surf->total_size = surf->surf_size;
   surf->alignment = surf->surf_alignment;

   /* Ensure the offsets are always 0 if not available. */
   surf->dcc_offset = surf->display_dcc_offset = 0;
   surf->fmask_offset = surf->cmask_offset = 0;
   surf->htile_offset = 0;

   if (surf->htile_size) {
      surf->htile_offset = align64(surf->total_size, surf->htile_alignment);
      surf->total_size = surf->htile_offset + surf->htile_size;
      surf->alignment = MAX2(surf->alignment, surf->htile_alignment);
   }

   if (surf->fmask_size) {
      assert(config->info.samples >= 2);
      surf->fmask_offset = align64(surf->total_size, surf->fmask_alignment);
      surf->total_size = surf->fmask_offset + surf->fmask_size;
      surf->alignment = MAX2(surf->alignment, surf->fmask_alignment);
   }

   /* Single-sample CMASK is in a separate buffer. */
   if (surf->cmask_size && config->info.samples >= 2) {
      surf->cmask_offset = align64(surf->total_size, surf->cmask_alignment);
      surf->total_size = surf->cmask_offset + surf->cmask_size;
      surf->alignment = MAX2(surf->alignment, surf->cmask_alignment);
   }

   if (surf->is_displayable)
      surf->flags |= RADEON_SURF_SCANOUT;

   if (surf->dcc_size &&
       /* dcc_size is computed on GFX9+ only if it's displayable. */
       (info->chip_class >= GFX9 || !get_display_flag(config, surf))) {
      /* It's better when displayable DCC is immediately after
       * the image due to hw-specific reasons.
       */
      if (info->chip_class >= GFX9 && surf->u.gfx9.dcc_retile_num_elements) {
         /* Add space for the displayable DCC buffer. */
         surf->display_dcc_offset = align64(surf->total_size, surf->u.gfx9.display_dcc_alignment);
         surf->total_size = surf->display_dcc_offset + surf->u.gfx9.display_dcc_size;
      }

      surf->dcc_offset = align64(surf->total_size, surf->dcc_alignment);
      surf->total_size = surf->dcc_offset + surf->dcc_size;
      surf->alignment = MAX2(surf->alignment, surf->dcc_alignment);
   }

   return 0;
}

/* This is meant to be used for disabling DCC. */
void ac_surface_zero_dcc_fields(struct radeon_surf *surf)
{
   surf->dcc_offset = 0;
   surf->display_dcc_offset = 0;
}

static unsigned eg_tile_split(unsigned tile_split)
{
   switch (tile_split) {
   case 0:
      tile_split = 64;
      break;
   case 1:
      tile_split = 128;
      break;
   case 2:
      tile_split = 256;
      break;
   case 3:
      tile_split = 512;
      break;
   default:
   case 4:
      tile_split = 1024;
      break;
   case 5:
      tile_split = 2048;
      break;
   case 6:
      tile_split = 4096;
      break;
   }
   return tile_split;
}

static unsigned eg_tile_split_rev(unsigned eg_tile_split)
{
   switch (eg_tile_split) {
   case 64:
      return 0;
   case 128:
      return 1;
   case 256:
      return 2;
   case 512:
      return 3;
   default:
   case 1024:
      return 4;
   case 2048:
      return 5;
   case 4096:
      return 6;
   }
}

#define AMDGPU_TILING_DCC_MAX_COMPRESSED_BLOCK_SIZE_SHIFT 45
#define AMDGPU_TILING_DCC_MAX_COMPRESSED_BLOCK_SIZE_MASK  0x3

/* This should be called before ac_compute_surface. */
void ac_surface_set_bo_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                uint64_t tiling_flags, enum radeon_surf_mode *mode)
{
   bool scanout;

   if (info->chip_class >= GFX9) {
      surf->u.gfx9.surf.swizzle_mode = AMDGPU_TILING_GET(tiling_flags, SWIZZLE_MODE);
      surf->u.gfx9.dcc.independent_64B_blocks =
         AMDGPU_TILING_GET(tiling_flags, DCC_INDEPENDENT_64B);
      surf->u.gfx9.dcc.independent_128B_blocks =
         AMDGPU_TILING_GET(tiling_flags, DCC_INDEPENDENT_128B);
      surf->u.gfx9.dcc.max_compressed_block_size =
         AMDGPU_TILING_GET(tiling_flags, DCC_MAX_COMPRESSED_BLOCK_SIZE);
      surf->u.gfx9.display_dcc_pitch_max = AMDGPU_TILING_GET(tiling_flags, DCC_PITCH_MAX);
      scanout = AMDGPU_TILING_GET(tiling_flags, SCANOUT);
      *mode =
         surf->u.gfx9.surf.swizzle_mode > 0 ? RADEON_SURF_MODE_2D : RADEON_SURF_MODE_LINEAR_ALIGNED;
   } else {
      surf->u.legacy.pipe_config = AMDGPU_TILING_GET(tiling_flags, PIPE_CONFIG);
      surf->u.legacy.bankw = 1 << AMDGPU_TILING_GET(tiling_flags, BANK_WIDTH);
      surf->u.legacy.bankh = 1 << AMDGPU_TILING_GET(tiling_flags, BANK_HEIGHT);
      surf->u.legacy.tile_split = eg_tile_split(AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT));
      surf->u.legacy.mtilea = 1 << AMDGPU_TILING_GET(tiling_flags, MACRO_TILE_ASPECT);
      surf->u.legacy.num_banks = 2 << AMDGPU_TILING_GET(tiling_flags, NUM_BANKS);
      scanout = AMDGPU_TILING_GET(tiling_flags, MICRO_TILE_MODE) == 0; /* DISPLAY */

      if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == 4) /* 2D_TILED_THIN1 */
         *mode = RADEON_SURF_MODE_2D;
      else if (AMDGPU_TILING_GET(tiling_flags, ARRAY_MODE) == 2) /* 1D_TILED_THIN1 */
         *mode = RADEON_SURF_MODE_1D;
      else
         *mode = RADEON_SURF_MODE_LINEAR_ALIGNED;
   }

   if (scanout)
      surf->flags |= RADEON_SURF_SCANOUT;
   else
      surf->flags &= ~RADEON_SURF_SCANOUT;
}

void ac_surface_get_bo_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                uint64_t *tiling_flags)
{
   *tiling_flags = 0;

   if (info->chip_class >= GFX9) {
      uint64_t dcc_offset = 0;

      if (surf->dcc_offset) {
         dcc_offset = surf->display_dcc_offset ? surf->display_dcc_offset : surf->dcc_offset;
         assert((dcc_offset >> 8) != 0 && (dcc_offset >> 8) < (1 << 24));
      }

      *tiling_flags |= AMDGPU_TILING_SET(SWIZZLE_MODE, surf->u.gfx9.surf.swizzle_mode);
      *tiling_flags |= AMDGPU_TILING_SET(DCC_OFFSET_256B, dcc_offset >> 8);
      *tiling_flags |= AMDGPU_TILING_SET(DCC_PITCH_MAX, surf->u.gfx9.display_dcc_pitch_max);
      *tiling_flags |=
         AMDGPU_TILING_SET(DCC_INDEPENDENT_64B, surf->u.gfx9.dcc.independent_64B_blocks);
      *tiling_flags |=
         AMDGPU_TILING_SET(DCC_INDEPENDENT_128B, surf->u.gfx9.dcc.independent_128B_blocks);
      *tiling_flags |= AMDGPU_TILING_SET(DCC_MAX_COMPRESSED_BLOCK_SIZE,
                                         surf->u.gfx9.dcc.max_compressed_block_size);
      *tiling_flags |= AMDGPU_TILING_SET(SCANOUT, (surf->flags & RADEON_SURF_SCANOUT) != 0);
   } else {
      if (surf->u.legacy.level[0].mode >= RADEON_SURF_MODE_2D)
         *tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 4); /* 2D_TILED_THIN1 */
      else if (surf->u.legacy.level[0].mode >= RADEON_SURF_MODE_1D)
         *tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 2); /* 1D_TILED_THIN1 */
      else
         *tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 1); /* LINEAR_ALIGNED */

      *tiling_flags |= AMDGPU_TILING_SET(PIPE_CONFIG, surf->u.legacy.pipe_config);
      *tiling_flags |= AMDGPU_TILING_SET(BANK_WIDTH, util_logbase2(surf->u.legacy.bankw));
      *tiling_flags |= AMDGPU_TILING_SET(BANK_HEIGHT, util_logbase2(surf->u.legacy.bankh));
      if (surf->u.legacy.tile_split)
         *tiling_flags |=
            AMDGPU_TILING_SET(TILE_SPLIT, eg_tile_split_rev(surf->u.legacy.tile_split));
      *tiling_flags |= AMDGPU_TILING_SET(MACRO_TILE_ASPECT, util_logbase2(surf->u.legacy.mtilea));
      *tiling_flags |= AMDGPU_TILING_SET(NUM_BANKS, util_logbase2(surf->u.legacy.num_banks) - 1);

      if (surf->flags & RADEON_SURF_SCANOUT)
         *tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 0); /* DISPLAY_MICRO_TILING */
      else
         *tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 1); /* THIN_MICRO_TILING */
   }
}

static uint32_t ac_get_umd_metadata_word1(const struct radeon_info *info)
{
   return (ATI_VENDOR_ID << 16) | info->pci_id;
}

/* This should be called after ac_compute_surface. */
bool ac_surface_set_umd_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                 unsigned num_storage_samples, unsigned num_mipmap_levels,
                                 unsigned size_metadata, uint32_t metadata[64])
{
   uint32_t *desc = &metadata[2];
   uint64_t offset;

   if (info->chip_class >= GFX9)
      offset = surf->u.gfx9.surf_offset;
   else
      offset = surf->u.legacy.level[0].offset;

   if (offset ||                 /* Non-zero planes ignore metadata. */
       size_metadata < 10 * 4 || /* at least 2(header) + 8(desc) dwords */
       metadata[0] == 0 ||       /* invalid version number */
       metadata[1] != ac_get_umd_metadata_word1(info)) /* invalid PCI ID */ {
      /* Disable DCC because it might not be enabled. */
      ac_surface_zero_dcc_fields(surf);

      /* Don't report an error if the texture comes from an incompatible driver,
       * but this might not work.
       */
      return true;
   }

   /* Validate that sample counts and the number of mipmap levels match. */
   unsigned desc_last_level = G_008F1C_LAST_LEVEL(desc[3]);
   unsigned type = G_008F1C_TYPE(desc[3]);

   if (type == V_008F1C_SQ_RSRC_IMG_2D_MSAA || type == V_008F1C_SQ_RSRC_IMG_2D_MSAA_ARRAY) {
      unsigned log_samples = util_logbase2(MAX2(1, num_storage_samples));

      if (desc_last_level != log_samples) {
         fprintf(stderr,
                 "amdgpu: invalid MSAA texture import, "
                 "metadata has log2(samples) = %u, the caller set %u\n",
                 desc_last_level, log_samples);
         return false;
      }
   } else {
      if (desc_last_level != num_mipmap_levels - 1) {
         fprintf(stderr,
                 "amdgpu: invalid mipmapped texture import, "
                 "metadata has last_level = %u, the caller set %u\n",
                 desc_last_level, num_mipmap_levels - 1);
         return false;
      }
   }

   if (info->chip_class >= GFX8 && G_008F28_COMPRESSION_EN(desc[6])) {
      /* Read DCC information. */
      switch (info->chip_class) {
      case GFX8:
         surf->dcc_offset = (uint64_t)desc[7] << 8;
         break;

      case GFX9:
         surf->dcc_offset =
            ((uint64_t)desc[7] << 8) | ((uint64_t)G_008F24_META_DATA_ADDRESS(desc[5]) << 40);
         surf->u.gfx9.dcc.pipe_aligned = G_008F24_META_PIPE_ALIGNED(desc[5]);
         surf->u.gfx9.dcc.rb_aligned = G_008F24_META_RB_ALIGNED(desc[5]);

         /* If DCC is unaligned, this can only be a displayable image. */
         if (!surf->u.gfx9.dcc.pipe_aligned && !surf->u.gfx9.dcc.rb_aligned)
            assert(surf->is_displayable);
         break;

      case GFX10:
      case GFX10_3:
         surf->dcc_offset =
            ((uint64_t)G_00A018_META_DATA_ADDRESS_LO(desc[6]) << 8) | ((uint64_t)desc[7] << 16);
         surf->u.gfx9.dcc.pipe_aligned = G_00A018_META_PIPE_ALIGNED(desc[6]);
         break;

      default:
         assert(0);
         return false;
      }
   } else {
      /* Disable DCC. dcc_offset is always set by texture_from_handle
       * and must be cleared here.
       */
      ac_surface_zero_dcc_fields(surf);
   }

   return true;
}

void ac_surface_get_umd_metadata(const struct radeon_info *info, struct radeon_surf *surf,
                                 unsigned num_mipmap_levels, uint32_t desc[8],
                                 unsigned *size_metadata, uint32_t metadata[64])
{
   /* Clear the base address and set the relative DCC offset. */
   desc[0] = 0;
   desc[1] &= C_008F14_BASE_ADDRESS_HI;

   switch (info->chip_class) {
   case GFX6:
   case GFX7:
      break;
   case GFX8:
      desc[7] = surf->dcc_offset >> 8;
      break;
   case GFX9:
      desc[7] = surf->dcc_offset >> 8;
      desc[5] &= C_008F24_META_DATA_ADDRESS;
      desc[5] |= S_008F24_META_DATA_ADDRESS(surf->dcc_offset >> 40);
      break;
   case GFX10:
   case GFX10_3:
      desc[6] &= C_00A018_META_DATA_ADDRESS_LO;
      desc[6] |= S_00A018_META_DATA_ADDRESS_LO(surf->dcc_offset >> 8);
      desc[7] = surf->dcc_offset >> 16;
      break;
   default:
      assert(0);
   }

   /* Metadata image format format version 1:
    * [0] = 1 (metadata format identifier)
    * [1] = (VENDOR_ID << 16) | PCI_ID
    * [2:9] = image descriptor for the whole resource
    *         [2] is always 0, because the base address is cleared
    *         [9] is the DCC offset bits [39:8] from the beginning of
    *             the buffer
    * [10:10+LAST_LEVEL] = mipmap level offset bits [39:8] for each level
    */

   metadata[0] = 1; /* metadata image format version 1 */

   /* Tiling modes are ambiguous without a PCI ID. */
   metadata[1] = ac_get_umd_metadata_word1(info);

   /* Dwords [2:9] contain the image descriptor. */
   memcpy(&metadata[2], desc, 8 * 4);
   *size_metadata = 10 * 4;

   /* Dwords [10:..] contain the mipmap level offsets. */
   if (info->chip_class <= GFX8) {
      for (unsigned i = 0; i < num_mipmap_levels; i++)
         metadata[10 + i] = surf->u.legacy.level[i].offset >> 8;

      *size_metadata += num_mipmap_levels * 4;
   }
}

void ac_surface_override_offset_stride(const struct radeon_info *info, struct radeon_surf *surf,
                                       unsigned num_mipmap_levels, uint64_t offset, unsigned pitch)
{
   if (info->chip_class >= GFX9) {
      if (pitch) {
         surf->u.gfx9.surf_pitch = pitch;
         if (num_mipmap_levels == 1)
            surf->u.gfx9.surf.epitch = pitch - 1;
         surf->u.gfx9.surf_slice_size = (uint64_t)pitch * surf->u.gfx9.surf_height * surf->bpe;
      }
      surf->u.gfx9.surf_offset = offset;
      if (surf->u.gfx9.stencil_offset)
         surf->u.gfx9.stencil_offset += offset;
   } else {
      if (pitch) {
         surf->u.legacy.level[0].nblk_x = pitch;
         surf->u.legacy.level[0].slice_size_dw =
            ((uint64_t)pitch * surf->u.legacy.level[0].nblk_y * surf->bpe) / 4;
      }

      if (offset) {
         for (unsigned i = 0; i < ARRAY_SIZE(surf->u.legacy.level); ++i)
            surf->u.legacy.level[i].offset += offset;
      }
   }

   if (surf->htile_offset)
      surf->htile_offset += offset;
   if (surf->fmask_offset)
      surf->fmask_offset += offset;
   if (surf->cmask_offset)
      surf->cmask_offset += offset;
   if (surf->dcc_offset)
      surf->dcc_offset += offset;
   if (surf->display_dcc_offset)
      surf->display_dcc_offset += offset;
}
