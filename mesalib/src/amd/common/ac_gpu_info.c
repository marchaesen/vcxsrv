/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
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

#include "ac_gpu_info.h"

#include "addrlib/src/amdgpu_asic_addr.h"
#include "sid.h"
#include "util/macros.h"
#include "util/u_cpu_detect.h"
#include "util/u_math.h"

#include <stdio.h>

#ifdef _WIN32
#define DRM_CAP_ADDFB2_MODIFIERS 0x10
#define DRM_CAP_SYNCOBJ 0x13
#define DRM_CAP_SYNCOBJ_TIMELINE 0x14
#define AMDGPU_GEM_DOMAIN_GTT 0x2
#define AMDGPU_GEM_DOMAIN_VRAM 0x4
#define AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED (1 << 0)
#define AMDGPU_GEM_CREATE_ENCRYPTED (1 << 10)
#define AMDGPU_HW_IP_GFX 0
#define AMDGPU_HW_IP_COMPUTE 1
#define AMDGPU_HW_IP_DMA 2
#define AMDGPU_HW_IP_UVD 3
#define AMDGPU_HW_IP_VCE 4
#define AMDGPU_HW_IP_UVD_ENC 5
#define AMDGPU_HW_IP_VCN_DEC 6
#define AMDGPU_HW_IP_VCN_ENC 7
#define AMDGPU_HW_IP_VCN_JPEG 8
#define AMDGPU_IDS_FLAGS_FUSION 0x1
#define AMDGPU_IDS_FLAGS_PREEMPTION 0x2
#define AMDGPU_IDS_FLAGS_TMZ 0x4
#define AMDGPU_INFO_FW_VCE 0x1
#define AMDGPU_INFO_FW_UVD 0x2
#define AMDGPU_INFO_FW_GFX_ME 0x04
#define AMDGPU_INFO_FW_GFX_PFP 0x05
#define AMDGPU_INFO_FW_GFX_CE 0x06
#define AMDGPU_INFO_DEV_INFO 0x16
#define AMDGPU_INFO_MEMORY 0x19
#define AMDGPU_INFO_VIDEO_CAPS_DECODE 0
#define AMDGPU_INFO_VIDEO_CAPS_ENCODE 1
struct drm_amdgpu_heap_info {
   uint64_t total_heap_size;
};
struct drm_amdgpu_memory_info {
   struct drm_amdgpu_heap_info vram;
   struct drm_amdgpu_heap_info cpu_accessible_vram;
   struct drm_amdgpu_heap_info gtt;
};
struct drm_amdgpu_info_device {
   uint32_t num_tcc_blocks;
   uint32_t pa_sc_tile_steering_override;
   uint64_t tcc_disabled_mask;
};
struct drm_amdgpu_info_hw_ip {
   uint32_t ib_start_alignment;
   uint32_t ib_size_alignment;
   uint32_t available_rings;
};
typedef struct _drmPciBusInfo {
   uint16_t domain;
   uint8_t bus;
   uint8_t dev;
   uint8_t func;
} drmPciBusInfo, *drmPciBusInfoPtr;
typedef struct _drmDevice {
   union {
      drmPciBusInfoPtr pci;
   } businfo;
} drmDevice, *drmDevicePtr;
enum amdgpu_sw_info {
   amdgpu_sw_info_address32_hi = 0,
};
typedef struct amdgpu_device *amdgpu_device_handle;
typedef struct amdgpu_bo *amdgpu_bo_handle;
struct amdgpu_bo_alloc_request {
   uint64_t alloc_size;
   uint64_t phys_alignment;
   uint32_t preferred_heap;
   uint64_t flags;
};
struct amdgpu_gds_resource_info {
   uint32_t gds_gfx_partition_size;
   uint32_t gds_total_size;
};
struct amdgpu_buffer_size_alignments {
   uint64_t size_local;
   uint64_t size_remote;
};
struct amdgpu_heap_info {
   uint64_t heap_size;
};
struct amdgpu_gpu_info {
   uint32_t asic_id;
   uint32_t chip_external_rev;
   uint32_t family_id;
   uint64_t ids_flags;
   uint64_t max_engine_clk;
   uint64_t max_memory_clk;
   uint32_t num_shader_engines;
   uint32_t num_shader_arrays_per_engine;
   uint32_t rb_pipes;
   uint32_t enabled_rb_pipes_mask;
   uint32_t gpu_counter_freq;
   uint32_t mc_arb_ramcfg;
   uint32_t gb_addr_cfg;
   uint32_t gb_tile_mode[32];
   uint32_t gb_macro_tile_mode[16];
   uint32_t cu_bitmap[4][4];
   uint32_t vram_type;
   uint32_t vram_bit_width;
   uint32_t ce_ram_size;
   uint32_t vce_harvest_config;
   uint32_t pci_rev_id;
};
static int drmGetCap(int fd, uint64_t capability, uint64_t *value)
{
   return -EINVAL;
}
static void drmFreeDevice(drmDevicePtr *device)
{
}
static int drmGetDevice2(int fd, uint32_t flags, drmDevicePtr *device)
{
   return -ENODEV;
}
static int amdgpu_bo_alloc(amdgpu_device_handle dev,
   struct amdgpu_bo_alloc_request *alloc_buffer,
   amdgpu_bo_handle *buf_handle)
{
   return -EINVAL;
}
static int amdgpu_bo_free(amdgpu_bo_handle buf_handle)
{
   return -EINVAL;
}
static int amdgpu_query_buffer_size_alignment(amdgpu_device_handle dev,
   struct amdgpu_buffer_size_alignments
   *info)
{
   return -EINVAL;
}
static int amdgpu_query_firmware_version(amdgpu_device_handle dev, unsigned fw_type,
   unsigned ip_instance, unsigned index,
   uint32_t *version, uint32_t *feature)
{
   return -EINVAL;
}
static int amdgpu_query_hw_ip_info(amdgpu_device_handle dev, unsigned type,
   unsigned ip_instance,
   struct drm_amdgpu_info_hw_ip *info)
{
   return -EINVAL;
}
static int amdgpu_query_heap_info(amdgpu_device_handle dev, uint32_t heap,
   uint32_t flags, struct amdgpu_heap_info *info)
{
   return -EINVAL;
}
static int amdgpu_query_gpu_info(amdgpu_device_handle dev,
   struct amdgpu_gpu_info *info)
{
   return -EINVAL;
}
static int amdgpu_query_info(amdgpu_device_handle dev, unsigned info_id,
   unsigned size, void *value)
{
   return -EINVAL;
}
static int amdgpu_query_sw_info(amdgpu_device_handle dev, enum amdgpu_sw_info info,
   void *value)
{
   return -EINVAL;
}
static int amdgpu_query_gds_info(amdgpu_device_handle dev,
   struct amdgpu_gds_resource_info *gds_info)
{
   return -EINVAL;
}
static int amdgpu_query_video_caps_info(amdgpu_device_handle dev, unsigned cap_type,
                                 unsigned size, void *value)
{
   return -EINVAL;
}
static const char *amdgpu_get_marketing_name(amdgpu_device_handle dev)
{
   return NULL;
}
#else
#include "drm-uapi/amdgpu_drm.h"
#include <amdgpu.h>
#include <xf86drm.h>
#endif

#define CIK_TILE_MODE_COLOR_2D 14

#define CIK__GB_TILE_MODE__PIPE_CONFIG(x)           (((x) >> 6) & 0x1f)
#define CIK__PIPE_CONFIG__ADDR_SURF_P2              0
#define CIK__PIPE_CONFIG__ADDR_SURF_P4_8x16         4
#define CIK__PIPE_CONFIG__ADDR_SURF_P4_16x16        5
#define CIK__PIPE_CONFIG__ADDR_SURF_P4_16x32        6
#define CIK__PIPE_CONFIG__ADDR_SURF_P4_32x32        7
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_16x16_8x16   8
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_8x16   9
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_8x16   10
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_16x16  11
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x16  12
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x32  13
#define CIK__PIPE_CONFIG__ADDR_SURF_P8_32x64_32x32  14
#define CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_8X16  16
#define CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_16X16 17

static unsigned cik_get_num_tile_pipes(struct amdgpu_gpu_info *info)
{
   unsigned mode2d = info->gb_tile_mode[CIK_TILE_MODE_COLOR_2D];

   switch (CIK__GB_TILE_MODE__PIPE_CONFIG(mode2d)) {
   case CIK__PIPE_CONFIG__ADDR_SURF_P2:
      return 2;
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_16x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_16x32:
   case CIK__PIPE_CONFIG__ADDR_SURF_P4_32x32:
      return 4;
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x16_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_8x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_16x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x32:
   case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x64_32x32:
      return 8;
   case CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_8X16:
   case CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_16X16:
      return 16;
   default:
      fprintf(stderr, "Invalid GFX7 pipe configuration, assuming P2\n");
      assert(!"this should never occur");
      return 2;
   }
}

static bool has_syncobj(int fd)
{
   uint64_t value;
   if (drmGetCap(fd, DRM_CAP_SYNCOBJ, &value))
      return false;
   return value ? true : false;
}

static bool has_timeline_syncobj(int fd)
{
   uint64_t value;
   if (drmGetCap(fd, DRM_CAP_SYNCOBJ_TIMELINE, &value))
      return false;
   return value ? true : false;
}

static bool has_modifiers(int fd)
{
   uint64_t value;
   if (drmGetCap(fd, DRM_CAP_ADDFB2_MODIFIERS, &value))
      return false;
   return value ? true : false;
}

static uint64_t fix_vram_size(uint64_t size)
{
   /* The VRAM size is underreported, so we need to fix it, because
    * it's used to compute the number of memory modules for harvesting.
    */
   return align64(size, 256 * 1024 * 1024);
}

static bool
has_tmz_support(amdgpu_device_handle dev,
                struct radeon_info *info,
                struct amdgpu_gpu_info *amdinfo)
{
   struct amdgpu_bo_alloc_request request = {0};
   int r;
   amdgpu_bo_handle bo;

   if (amdinfo->ids_flags & AMDGPU_IDS_FLAGS_TMZ)
      return true;

   /* AMDGPU_IDS_FLAGS_TMZ is supported starting from drm_minor 40 */
   if (info->drm_minor >= 40)
      return false;

   /* Find out ourselves if TMZ is enabled */
   if (info->chip_class < GFX9)
      return false;

   if (info->drm_minor < 36)
      return false;

   request.alloc_size = 256;
   request.phys_alignment = 1024;
   request.preferred_heap = AMDGPU_GEM_DOMAIN_VRAM;
   request.flags = AMDGPU_GEM_CREATE_ENCRYPTED;
   r = amdgpu_bo_alloc(dev, &request, &bo);
   if (r)
      return false;
   amdgpu_bo_free(bo);
   return true;
}


bool ac_query_gpu_info(int fd, void *dev_p, struct radeon_info *info,
                       struct amdgpu_gpu_info *amdinfo)
{
   struct drm_amdgpu_info_device device_info = {0};
   struct amdgpu_buffer_size_alignments alignment_info = {0};
   struct drm_amdgpu_info_hw_ip dma = {0}, compute = {0}, uvd = {0};
   struct drm_amdgpu_info_hw_ip uvd_enc = {0}, vce = {0}, vcn_dec = {0}, vcn_jpeg = {0};
   struct drm_amdgpu_info_hw_ip vcn_enc = {0}, gfx = {0};
   struct amdgpu_gds_resource_info gds = {0};
   uint32_t vce_version = 0, vce_feature = 0, uvd_version = 0, uvd_feature = 0;
   int r, i, j;
   amdgpu_device_handle dev = dev_p;
   drmDevicePtr devinfo;

   /* Get PCI info. */
   r = drmGetDevice2(fd, 0, &devinfo);
   if (r) {
      fprintf(stderr, "amdgpu: drmGetDevice2 failed.\n");
      return false;
   }
   info->pci_domain = devinfo->businfo.pci->domain;
   info->pci_bus = devinfo->businfo.pci->bus;
   info->pci_dev = devinfo->businfo.pci->dev;
   info->pci_func = devinfo->businfo.pci->func;
   drmFreeDevice(&devinfo);

   assert(info->drm_major == 3);
   info->is_amdgpu = true;

   /* Query hardware and driver information. */
   r = amdgpu_query_gpu_info(dev, amdinfo);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_gpu_info failed.\n");
      return false;
   }

   r = amdgpu_query_info(dev, AMDGPU_INFO_DEV_INFO, sizeof(device_info), &device_info);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_info(dev_info) failed.\n");
      return false;
   }

   r = amdgpu_query_buffer_size_alignment(dev, &alignment_info);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_buffer_size_alignment failed.\n");
      return false;
   }

   r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_DMA, 0, &dma);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(dma) failed.\n");
      return false;
   }

   r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_GFX, 0, &gfx);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(gfx) failed.\n");
      return false;
   }

   r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_COMPUTE, 0, &compute);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(compute) failed.\n");
      return false;
   }

   r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_UVD, 0, &uvd);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(uvd) failed.\n");
      return false;
   }

   if (info->drm_minor >= 17) {
      r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_UVD_ENC, 0, &uvd_enc);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(uvd_enc) failed.\n");
         return false;
      }
   }

   if (info->drm_minor >= 17) {
      r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCN_DEC, 0, &vcn_dec);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vcn_dec) failed.\n");
         return false;
      }
   }

   if (info->drm_minor >= 17) {
      r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCN_ENC, 0, &vcn_enc);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vcn_enc) failed.\n");
         return false;
      }
   }

   if (info->drm_minor >= 27) {
      r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCN_JPEG, 0, &vcn_jpeg);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vcn_jpeg) failed.\n");
         return false;
      }
   }

   r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_ME, 0, 0, &info->me_fw_version,
                                     &info->me_fw_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(me) failed.\n");
      return false;
   }

   r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_PFP, 0, 0, &info->pfp_fw_version,
                                     &info->pfp_fw_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(pfp) failed.\n");
      return false;
   }

   r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_CE, 0, 0, &info->ce_fw_version,
                                     &info->ce_fw_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(ce) failed.\n");
      return false;
   }

   r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_UVD, 0, 0, &uvd_version, &uvd_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(uvd) failed.\n");
      return false;
   }

   r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCE, 0, &vce);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vce) failed.\n");
      return false;
   }

   r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_VCE, 0, 0, &vce_version, &vce_feature);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(vce) failed.\n");
      return false;
   }

   r = amdgpu_query_sw_info(dev, amdgpu_sw_info_address32_hi, &info->address32_hi);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_sw_info(address32_hi) failed.\n");
      return false;
   }

   r = amdgpu_query_gds_info(dev, &gds);
   if (r) {
      fprintf(stderr, "amdgpu: amdgpu_query_gds_info failed.\n");
      return false;
   }

   if (info->drm_minor >= 9) {
      struct drm_amdgpu_memory_info meminfo = {0};

      r = amdgpu_query_info(dev, AMDGPU_INFO_MEMORY, sizeof(meminfo), &meminfo);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_info(memory) failed.\n");
         return false;
      }

      /* Note: usable_heap_size values can be random and can't be relied on. */
      info->gart_size = meminfo.gtt.total_heap_size;
      info->vram_size = fix_vram_size(meminfo.vram.total_heap_size);
      info->vram_vis_size = meminfo.cpu_accessible_vram.total_heap_size;
   } else {
      /* This is a deprecated interface, which reports usable sizes
       * (total minus pinned), but the pinned size computation is
       * buggy, so the values returned from these functions can be
       * random.
       */
      struct amdgpu_heap_info vram, vram_vis, gtt;

      r = amdgpu_query_heap_info(dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &vram);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_heap_info(vram) failed.\n");
         return false;
      }

      r = amdgpu_query_heap_info(dev, AMDGPU_GEM_DOMAIN_VRAM, AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
                                 &vram_vis);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_heap_info(vram_vis) failed.\n");
         return false;
      }

      r = amdgpu_query_heap_info(dev, AMDGPU_GEM_DOMAIN_GTT, 0, &gtt);
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_heap_info(gtt) failed.\n");
         return false;
      }

      info->gart_size = gtt.heap_size;
      info->vram_size = fix_vram_size(vram.heap_size);
      info->vram_vis_size = vram_vis.heap_size;
   }

   info->gart_size_kb = DIV_ROUND_UP(info->gart_size, 1024);
   info->vram_size_kb = DIV_ROUND_UP(info->vram_size, 1024);

   if (info->drm_minor >= 41) {
      r = amdgpu_query_video_caps_info(dev, AMDGPU_INFO_VIDEO_CAPS_DECODE,
            sizeof(info->dec_caps), &(info->dec_caps));
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_video_caps_info for decode failed.\n");
         return r;
      }

      r = amdgpu_query_video_caps_info(dev, AMDGPU_INFO_VIDEO_CAPS_ENCODE,
            sizeof(info->enc_caps), &(info->enc_caps));
      if (r) {
         fprintf(stderr, "amdgpu: amdgpu_query_video_caps_info for encode failed.\n");
         return r;
      }
   }

   /* Add some margin of error, though this shouldn't be needed in theory. */
   info->all_vram_visible = info->vram_size * 0.9 < info->vram_vis_size;

   util_cpu_detect();

   /* Set chip identification. */
   info->pci_id = amdinfo->asic_id; /* TODO: is this correct? */
   info->pci_rev_id = amdinfo->pci_rev_id;
   info->vce_harvest_config = amdinfo->vce_harvest_config;

#define identify_chip2(asic, chipname)                                                             \
   if (ASICREV_IS(amdinfo->chip_external_rev, asic)) {                                             \
      info->family = CHIP_##chipname;                                                              \
      info->name = #chipname;                                                                      \
   }
#define identify_chip(chipname) identify_chip2(chipname, chipname)

   switch (amdinfo->family_id) {
   case FAMILY_SI:
      identify_chip(TAHITI);
      identify_chip(PITCAIRN);
      identify_chip2(CAPEVERDE, VERDE);
      identify_chip(OLAND);
      identify_chip(HAINAN);
      break;
   case FAMILY_CI:
      identify_chip(BONAIRE);
      identify_chip(HAWAII);
      break;
   case FAMILY_KV:
      identify_chip2(SPECTRE, KAVERI);
      identify_chip2(SPOOKY, KAVERI);
      identify_chip2(KALINDI, KABINI);
      identify_chip2(GODAVARI, KABINI);
      break;
   case FAMILY_VI:
      identify_chip(ICELAND);
      identify_chip(TONGA);
      identify_chip(FIJI);
      identify_chip(POLARIS10);
      identify_chip(POLARIS11);
      identify_chip(POLARIS12);
      identify_chip(VEGAM);
      break;
   case FAMILY_CZ:
      identify_chip(CARRIZO);
      identify_chip(STONEY);
      break;
   case FAMILY_AI:
      identify_chip(VEGA10);
      identify_chip(VEGA12);
      identify_chip(VEGA20);
      identify_chip(ARCTURUS);
      identify_chip(ALDEBARAN);
      break;
   case FAMILY_RV:
      identify_chip(RAVEN);
      identify_chip(RAVEN2);
      identify_chip(RENOIR);
      break;
   case FAMILY_NV:
      identify_chip(NAVI10);
      identify_chip(NAVI12);
      identify_chip(NAVI14);
      identify_chip(SIENNA_CICHLID);
      identify_chip(NAVY_FLOUNDER);
      identify_chip(DIMGREY_CAVEFISH);
      identify_chip(BEIGE_GOBY);
      break;
   case FAMILY_VGH:
      identify_chip(VANGOGH);
      break;
   case FAMILY_YC:
      identify_chip(YELLOW_CARP);
      break;
   }

   if (!info->name) {
      fprintf(stderr, "amdgpu: unknown (family_id, chip_external_rev): (%u, %u)\n",
              amdinfo->family_id, amdinfo->chip_external_rev);
      return false;
   }

   if (info->family >= CHIP_SIENNA_CICHLID)
      info->chip_class = GFX10_3;
   else if (info->family >= CHIP_NAVI10)
      info->chip_class = GFX10;
   else if (info->family >= CHIP_VEGA10)
      info->chip_class = GFX9;
   else if (info->family >= CHIP_TONGA)
      info->chip_class = GFX8;
   else if (info->family >= CHIP_BONAIRE)
      info->chip_class = GFX7;
   else if (info->family >= CHIP_TAHITI)
      info->chip_class = GFX6;
   else {
      fprintf(stderr, "amdgpu: Unknown family.\n");
      return false;
   }

   info->smart_access_memory = info->all_vram_visible &&
                               info->chip_class >= GFX10_3 &&
                               util_get_cpu_caps()->family >= CPU_AMD_ZEN3 &&
                               util_get_cpu_caps()->family < CPU_AMD_LAST;

   info->family_id = amdinfo->family_id;
   info->chip_external_rev = amdinfo->chip_external_rev;
   info->marketing_name = amdgpu_get_marketing_name(dev);
   info->is_pro_graphics = info->marketing_name && (strstr(info->marketing_name, "Pro") ||
                                                    strstr(info->marketing_name, "PRO") ||
                                                    strstr(info->marketing_name, "Frontier"));

   /* Set which chips have dedicated VRAM. */
   info->has_dedicated_vram = !(amdinfo->ids_flags & AMDGPU_IDS_FLAGS_FUSION);

   /* The kernel can split large buffers in VRAM but not in GTT, so large
    * allocations can fail or cause buffer movement failures in the kernel.
    */
   if (info->has_dedicated_vram)
      info->max_alloc_size = info->vram_size * 0.8;
   else
      info->max_alloc_size = info->gart_size * 0.7;

   info->vram_type = amdinfo->vram_type;
   info->vram_bit_width = amdinfo->vram_bit_width;
   info->ce_ram_size = amdinfo->ce_ram_size;

   /* Set which chips have uncached device memory. */
   info->has_l2_uncached = info->chip_class >= GFX9;

   /* Set hardware information. */
   info->gds_size = gds.gds_total_size;
   info->gds_gfx_partition_size = gds.gds_gfx_partition_size;
   /* convert the shader/memory clocks from KHz to MHz */
   info->max_shader_clock = amdinfo->max_engine_clk / 1000;
   info->max_memory_clock = amdinfo->max_memory_clk / 1000;
   info->max_tcc_blocks = device_info.num_tcc_blocks;
   info->max_se = amdinfo->num_shader_engines;
   info->max_sa_per_se = amdinfo->num_shader_arrays_per_engine;
   info->uvd_fw_version = uvd.available_rings ? uvd_version : 0;
   info->vce_fw_version = vce.available_rings ? vce_version : 0;
   info->has_video_hw.uvd_decode = uvd.available_rings != 0;
   info->has_video_hw.vcn_decode = vcn_dec.available_rings != 0;
   info->has_video_hw.jpeg_decode = vcn_jpeg.available_rings != 0;
   info->has_video_hw.vce_encode = vce.available_rings != 0;
   info->has_video_hw.uvd_encode = uvd_enc.available_rings != 0;
   info->has_video_hw.vcn_encode = vcn_enc.available_rings != 0;
   info->has_userptr = true;
   info->has_syncobj = has_syncobj(fd);
   info->has_timeline_syncobj = has_timeline_syncobj(fd);
   info->has_fence_to_handle = info->has_syncobj && info->drm_minor >= 21;
   info->has_local_buffers = info->drm_minor >= 20;
   info->kernel_flushes_hdp_before_ib = true;
   info->htile_cmask_support_1d_tiling = true;
   info->si_TA_CS_BC_BASE_ADDR_allowed = true;
   info->has_bo_metadata = true;
   info->has_gpu_reset_status_query = true;
   info->has_eqaa_surface_allocator = true;
   info->has_format_bc1_through_bc7 = true;
   /* DRM 3.1.0 doesn't flush TC for GFX8 correctly. */
   info->kernel_flushes_tc_l2_after_ib = info->chip_class != GFX8 || info->drm_minor >= 2;
   info->has_indirect_compute_dispatch = true;
   /* GFX6 doesn't support unaligned loads. */
   info->has_unaligned_shader_loads = info->chip_class != GFX6;
   /* Disable sparse mappings on GFX6 due to VM faults in CP DMA. Enable them once
    * these faults are mitigated in software.
    */
   info->has_sparse_vm_mappings = info->chip_class >= GFX7 && info->drm_minor >= 13;
   info->has_2d_tiling = true;
   info->has_read_registers_query = true;
   info->has_scheduled_fence_dependency = info->drm_minor >= 28;
   info->mid_command_buffer_preemption_enabled = amdinfo->ids_flags & AMDGPU_IDS_FLAGS_PREEMPTION;
   info->has_tmz_support = has_tmz_support(dev, info, amdinfo);
   info->kernel_has_modifiers = has_modifiers(fd);
   info->has_graphics = gfx.available_rings > 0;

   info->pa_sc_tile_steering_override = device_info.pa_sc_tile_steering_override;
   info->max_render_backends = amdinfo->rb_pipes;
   /* The value returned by the kernel driver was wrong. */
   if (info->family == CHIP_KAVERI)
      info->max_render_backends = 2;

   /* Guess the number of enabled SEs because the kernel doesn't tell us. */
   if (info->chip_class >= GFX10_3 && info->max_se > 1) {
      unsigned num_rbs_per_se = info->max_render_backends / info->max_se;
      info->num_se = util_bitcount(amdinfo->enabled_rb_pipes_mask) / num_rbs_per_se;
   } else {
      info->num_se = info->max_se;
   }

   info->clock_crystal_freq = amdinfo->gpu_counter_freq;
   if (!info->clock_crystal_freq) {
      fprintf(stderr, "amdgpu: clock crystal frequency is 0, timestamps will be wrong\n");
      info->clock_crystal_freq = 1;
   }
   if (info->chip_class >= GFX10) {
      info->tcc_cache_line_size = 128;

      if (info->drm_minor >= 35) {
         info->num_tcc_blocks = info->max_tcc_blocks - util_bitcount64(device_info.tcc_disabled_mask);
      } else {
         /* This is a hack, but it's all we can do without a kernel upgrade. */
         info->num_tcc_blocks = info->vram_size / (512 * 1024 * 1024);
         if (info->num_tcc_blocks > info->max_tcc_blocks)
            info->num_tcc_blocks /= 2;
      }
   } else {
      if (!info->has_graphics && info->family >= CHIP_ALDEBARAN)
         info->tcc_cache_line_size = 128;
      else
         info->tcc_cache_line_size = 64;

      info->num_tcc_blocks = info->max_tcc_blocks;
   }

   info->tcc_rb_non_coherent = !util_is_power_of_two_or_zero(info->num_tcc_blocks);

   switch (info->family) {
   case CHIP_TAHITI:
   case CHIP_PITCAIRN:
   case CHIP_OLAND:
   case CHIP_HAWAII:
   case CHIP_KABINI:
   case CHIP_TONGA:
   case CHIP_STONEY:
   case CHIP_RAVEN2:
      info->l2_cache_size = info->num_tcc_blocks * 64 * 1024;
      break;
   case CHIP_VERDE:
   case CHIP_HAINAN:
   case CHIP_BONAIRE:
   case CHIP_KAVERI:
   case CHIP_ICELAND:
   case CHIP_CARRIZO:
   case CHIP_FIJI:
   case CHIP_POLARIS12:
   case CHIP_VEGAM:
      info->l2_cache_size = info->num_tcc_blocks * 128 * 1024;
      break;
   default:
      info->l2_cache_size = info->num_tcc_blocks * 256 * 1024;
      break;
   }

   info->l1_cache_size = 16384;

   info->mc_arb_ramcfg = amdinfo->mc_arb_ramcfg;
   info->gb_addr_config = amdinfo->gb_addr_cfg;
   if (info->chip_class >= GFX9) {
      info->num_tile_pipes = 1 << G_0098F8_NUM_PIPES(amdinfo->gb_addr_cfg);
      info->pipe_interleave_bytes = 256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(amdinfo->gb_addr_cfg);
   } else {
      info->num_tile_pipes = cik_get_num_tile_pipes(amdinfo);
      info->pipe_interleave_bytes = 256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX6(amdinfo->gb_addr_cfg);
   }
   info->r600_has_virtual_memory = true;

   /* LDS is 64KB per CU (4 SIMDs), which is 16KB per SIMD (usage above
    * 16KB makes some SIMDs unoccupied).
    *
    * LDS is 128KB in WGP mode and 64KB in CU mode. Assume the WGP mode is used.
    */
   info->lds_size_per_workgroup = info->chip_class >= GFX10 ? 128 * 1024 : 64 * 1024;
   /* lds_encode_granularity is the block size used for encoding registers.
    * lds_alloc_granularity is what the hardware will align the LDS size to.
    */
   info->lds_encode_granularity = info->chip_class >= GFX7 ? 128 * 4 : 64 * 4;
   info->lds_alloc_granularity = info->chip_class >= GFX10_3 ? 256 * 4 : info->lds_encode_granularity;

   assert(util_is_power_of_two_or_zero(dma.available_rings + 1));
   assert(util_is_power_of_two_or_zero(compute.available_rings + 1));

   info->num_rings[RING_GFX] = util_bitcount(gfx.available_rings);
   info->num_rings[RING_COMPUTE] = util_bitcount(compute.available_rings);
   info->num_rings[RING_DMA] = util_bitcount(dma.available_rings);
   info->num_rings[RING_UVD] = util_bitcount(uvd.available_rings);
   info->num_rings[RING_VCE] = util_bitcount(vce.available_rings);
   info->num_rings[RING_UVD_ENC] = util_bitcount(uvd_enc.available_rings);
   info->num_rings[RING_VCN_DEC] = util_bitcount(vcn_dec.available_rings);
   info->num_rings[RING_VCN_ENC] = util_bitcount(vcn_enc.available_rings);
   info->num_rings[RING_VCN_JPEG] = util_bitcount(vcn_jpeg.available_rings);

   /* This is "align_mask" copied from the kernel, maximums of all IP versions. */
   info->ib_pad_dw_mask[RING_GFX] = 0xff;
   info->ib_pad_dw_mask[RING_COMPUTE] = 0xff;
   info->ib_pad_dw_mask[RING_DMA] = 0xf;
   info->ib_pad_dw_mask[RING_UVD] = 0xf;
   info->ib_pad_dw_mask[RING_VCE] = 0x3f;
   info->ib_pad_dw_mask[RING_UVD_ENC] = 0x3f;
   info->ib_pad_dw_mask[RING_VCN_DEC] = 0xf;
   info->ib_pad_dw_mask[RING_VCN_ENC] = 0x3f;
   info->ib_pad_dw_mask[RING_VCN_JPEG] = 0xf;

   /* The mere presence of CLEAR_STATE in the IB causes random GPU hangs
    * on GFX6. Some CLEAR_STATE cause asic hang on radeon kernel, etc.
    * SPI_VS_OUT_CONFIG. So only enable GFX7 CLEAR_STATE on amdgpu kernel.
    */
   info->has_clear_state = info->chip_class >= GFX7;

   info->has_distributed_tess =
      info->chip_class >= GFX10 || (info->chip_class >= GFX8 && info->max_se >= 2);

   info->has_dcc_constant_encode =
      info->family == CHIP_RAVEN2 || info->family == CHIP_RENOIR || info->chip_class >= GFX10;

   info->has_rbplus = info->family == CHIP_STONEY || info->chip_class >= GFX9;

   /* Some chips have RB+ registers, but don't support RB+. Those must
    * always disable it.
    */
   info->rbplus_allowed =
      info->has_rbplus &&
      (info->family == CHIP_STONEY || info->family == CHIP_VEGA12 || info->family == CHIP_RAVEN ||
       info->family == CHIP_RAVEN2 || info->family == CHIP_RENOIR || info->chip_class >= GFX10_3);

   info->has_out_of_order_rast =
      info->chip_class >= GFX8 && info->chip_class <= GFX9 && info->max_se >= 2;

   /* Whether chips support double rate packed math instructions. */
   info->has_packed_math_16bit = info->chip_class >= GFX9;

   /* Whether chips support dot product instructions. A subset of these support a smaller
    * instruction encoding which accumulates with the destination.
    */
   info->has_accelerated_dot_product =
      info->family == CHIP_ARCTURUS || info->family == CHIP_ALDEBARAN ||
      info->family == CHIP_VEGA20 || info->family >= CHIP_NAVI12;

   /* TODO: Figure out how to use LOAD_CONTEXT_REG on GFX6-GFX7. */
   info->has_load_ctx_reg_pkt =
      info->chip_class >= GFX9 || (info->chip_class >= GFX8 && info->me_fw_feature >= 41);

   info->cpdma_prefetch_writes_memory = info->chip_class <= GFX8;

   info->has_gfx9_scissor_bug = info->family == CHIP_VEGA10 || info->family == CHIP_RAVEN;

   info->has_tc_compat_zrange_bug = info->chip_class >= GFX8 && info->chip_class <= GFX9;

   info->has_msaa_sample_loc_bug =
      (info->family >= CHIP_POLARIS10 && info->family <= CHIP_POLARIS12) ||
      info->family == CHIP_VEGA10 || info->family == CHIP_RAVEN;

   info->has_ls_vgpr_init_bug = info->family == CHIP_VEGA10 || info->family == CHIP_RAVEN;

   /* Drawing from 0-sized index buffers causes hangs on gfx10. */
   info->has_zero_index_buffer_bug = info->chip_class == GFX10;

   /* Whether chips are affected by the image load/sample/gather hw bug when
    * DCC is enabled (ie. WRITE_COMPRESS_ENABLE should be 0).
    */
   info->has_image_load_dcc_bug = info->family == CHIP_DIMGREY_CAVEFISH ||
                                  info->family == CHIP_VANGOGH ||
                                  info->family == CHIP_YELLOW_CARP;

   /* DB has a bug when ITERATE_256 is set to 1 that can cause a hang. The
    * workaround is to set DECOMPRESS_ON_Z_PLANES to 2 for 4X MSAA D/S images.
    */
   info->has_two_planes_iterate256_bug = info->chip_class == GFX10;

   /* GE has a bug when a legacy GS draw follows an NGG draw and it requires
    * a VGT_FLUSH to fix that.
    */
   info->has_vgt_flush_ngg_legacy_bug = info->chip_class == GFX10 ||
                                        info->family == CHIP_SIENNA_CICHLID;

   /* HW bug workaround when CS threadgroups > 256 threads and async compute
    * isn't used, i.e. only one compute job can run at a time.  If async
    * compute is possible, the threadgroup size must be limited to 256 threads
    * on all queues to avoid the bug.
    * Only GFX6 and certain GFX7 chips are affected.
    *
    * FIXME: RADV doesn't limit the number of threads for async compute.
    */
   info->has_cs_regalloc_hang_bug = info->chip_class == GFX6 ||
                                    info->family == CHIP_BONAIRE ||
                                    info->family == CHIP_KABINI;

   /* Support for GFX10.3 was added with F32_ME_FEATURE_VERSION_31 but the
    * feature version wasn't bumped.
    */
   info->has_32bit_predication = (info->chip_class >= GFX10 &&
                                  info->me_fw_feature >= 32) ||
                                 (info->chip_class == GFX9 &&
                                  info->me_fw_feature >= 52);

   /* Get the number of good compute units. */
   info->num_good_compute_units = 0;
   for (i = 0; i < info->max_se; i++) {
      for (j = 0; j < info->max_sa_per_se; j++) {
         /*
          * The cu bitmap in amd gpu info structure is
          * 4x4 size array, and it's usually suitable for Vega
          * ASICs which has 4*2 SE/SH layout.
          * But for Arcturus, SE/SH layout is changed to 8*1.
          * To mostly reduce the impact, we make it compatible
          * with current bitmap array as below:
          *    SE4,SH0 --> cu_bitmap[0][1]
          *    SE5,SH0 --> cu_bitmap[1][1]
          *    SE6,SH0 --> cu_bitmap[2][1]
          *    SE7,SH0 --> cu_bitmap[3][1]
          */
         info->cu_mask[i % 4][j + i / 4] = amdinfo->cu_bitmap[i % 4][j + i / 4];
         info->num_good_compute_units += util_bitcount(info->cu_mask[i][j]);
      }
   }

   /* On GFX10, only whole WGPs (in units of 2 CUs) can be disabled,
    * and max - min <= 2.
    */
   unsigned cu_group = info->chip_class >= GFX10 ? 2 : 1;
   info->max_good_cu_per_sa =
      DIV_ROUND_UP(info->num_good_compute_units, (info->num_se * info->max_sa_per_se * cu_group)) *
      cu_group;
   info->min_good_cu_per_sa =
      (info->num_good_compute_units / (info->num_se * info->max_sa_per_se * cu_group)) * cu_group;

   memcpy(info->si_tile_mode_array, amdinfo->gb_tile_mode, sizeof(amdinfo->gb_tile_mode));
   info->enabled_rb_mask = amdinfo->enabled_rb_pipes_mask;

   memcpy(info->cik_macrotile_mode_array, amdinfo->gb_macro_tile_mode,
          sizeof(amdinfo->gb_macro_tile_mode));

   info->pte_fragment_size = alignment_info.size_local;
   info->gart_page_size = alignment_info.size_remote;

   if (info->chip_class == GFX6)
      info->gfx_ib_pad_with_type2 = true;

   unsigned ib_align = 0;
   ib_align = MAX2(ib_align, gfx.ib_start_alignment);
   ib_align = MAX2(ib_align, gfx.ib_size_alignment);
   ib_align = MAX2(ib_align, compute.ib_start_alignment);
   ib_align = MAX2(ib_align, compute.ib_size_alignment);
   ib_align = MAX2(ib_align, dma.ib_start_alignment);
   ib_align = MAX2(ib_align, dma.ib_size_alignment);
   ib_align = MAX2(ib_align, uvd.ib_start_alignment);
   ib_align = MAX2(ib_align, uvd.ib_size_alignment);
   ib_align = MAX2(ib_align, uvd_enc.ib_start_alignment);
   ib_align = MAX2(ib_align, uvd_enc.ib_size_alignment);
   ib_align = MAX2(ib_align, vce.ib_start_alignment);
   ib_align = MAX2(ib_align, vce.ib_size_alignment);
   ib_align = MAX2(ib_align, vcn_dec.ib_start_alignment);
   ib_align = MAX2(ib_align, vcn_dec.ib_size_alignment);
   ib_align = MAX2(ib_align, vcn_enc.ib_start_alignment);
   ib_align = MAX2(ib_align, vcn_enc.ib_size_alignment);
   ib_align = MAX2(ib_align, vcn_jpeg.ib_start_alignment);
   ib_align = MAX2(ib_align, vcn_jpeg.ib_size_alignment);
   /* GFX10 and maybe GFX9 need this alignment for cache coherency. */
   if (info->chip_class >= GFX9)
      ib_align = MAX2(ib_align, info->tcc_cache_line_size);
   /* The kernel pads gfx and compute IBs to 256 dwords since:
    *   66f3b2d527154bd258a57c8815004b5964aa1cf5
    * Do the same.
    */
   ib_align = MAX2(ib_align, 1024);
   info->ib_alignment = ib_align;

   if ((info->drm_minor >= 31 && (info->family == CHIP_RAVEN || info->family == CHIP_RAVEN2 ||
                                  info->family == CHIP_RENOIR)) ||
       (info->drm_minor >= 34 && (info->family == CHIP_NAVI12 || info->family == CHIP_NAVI14)) ||
       info->chip_class >= GFX10_3) {
      if (info->max_render_backends == 1)
         info->use_display_dcc_unaligned = true;
      else
         info->use_display_dcc_with_retile_blit = true;
   }

   info->has_gds_ordered_append = info->chip_class >= GFX7 && info->drm_minor >= 29;

   if (info->chip_class >= GFX9 && info->has_graphics) {
      unsigned pc_lines = 0;

      switch (info->family) {
      case CHIP_VEGA10:
      case CHIP_VEGA12:
      case CHIP_VEGA20:
         pc_lines = 2048;
         break;
      case CHIP_RAVEN:
      case CHIP_RAVEN2:
      case CHIP_RENOIR:
      case CHIP_NAVI10:
      case CHIP_NAVI12:
      case CHIP_SIENNA_CICHLID:
      case CHIP_NAVY_FLOUNDER:
      case CHIP_DIMGREY_CAVEFISH:
         pc_lines = 1024;
         break;
      case CHIP_NAVI14:
      case CHIP_BEIGE_GOBY:
         pc_lines = 512;
         break;
      case CHIP_VANGOGH:
      case CHIP_YELLOW_CARP:
         pc_lines = 256;
         break;
      default:
         assert(0);
      }

      info->pc_lines = pc_lines;

      if (info->chip_class >= GFX10) {
         info->pbb_max_alloc_count = pc_lines / 3;
      } else {
         info->pbb_max_alloc_count = MIN2(128, pc_lines / (4 * info->max_se));
      }
   }

   if (info->chip_class >= GFX10_3)
      info->max_wave64_per_simd = 16;
   else if (info->chip_class == GFX10)
      info->max_wave64_per_simd = 20;
   else if (info->family >= CHIP_POLARIS10 && info->family <= CHIP_VEGAM)
      info->max_wave64_per_simd = 8;
   else
      info->max_wave64_per_simd = 10;

   if (info->chip_class >= GFX10) {
      info->num_physical_sgprs_per_simd = 128 * info->max_wave64_per_simd;
      info->min_sgpr_alloc = 128;
      info->sgpr_alloc_granularity = 128;
   } else if (info->chip_class >= GFX8) {
      info->num_physical_sgprs_per_simd = 800;
      info->min_sgpr_alloc = 16;
      info->sgpr_alloc_granularity = 16;
   } else {
      info->num_physical_sgprs_per_simd = 512;
      info->min_sgpr_alloc = 8;
      info->sgpr_alloc_granularity = 8;
   }

   info->has_3d_cube_border_color_mipmap = info->has_graphics || info->family == CHIP_ARCTURUS;
   info->max_sgpr_alloc = info->family == CHIP_TONGA || info->family == CHIP_ICELAND ? 96 : 104;

   if (!info->has_graphics && info->family >= CHIP_ALDEBARAN) {
      info->min_wave64_vgpr_alloc = 8;
      info->max_vgpr_alloc = 512;
      info->wave64_vgpr_alloc_granularity = 8;
   } else {
      info->min_wave64_vgpr_alloc = 4;
      info->max_vgpr_alloc = 256;
      info->wave64_vgpr_alloc_granularity = 4;
   }

   info->num_physical_wave64_vgprs_per_simd = info->chip_class >= GFX10 ? 512 : 256;
   info->num_simd_per_compute_unit = info->chip_class >= GFX10 ? 2 : 4;

   return true;
}

void ac_compute_driver_uuid(char *uuid, size_t size)
{
   char amd_uuid[] = "AMD-MESA-DRV";

   assert(size >= sizeof(amd_uuid));

   memset(uuid, 0, size);
   strncpy(uuid, amd_uuid, size);
}

void ac_compute_device_uuid(struct radeon_info *info, char *uuid, size_t size)
{
   uint32_t *uint_uuid = (uint32_t *)uuid;

   assert(size >= sizeof(uint32_t) * 4);

   /**
    * Use the device info directly instead of using a sha1. GL/VK UUIDs
    * are 16 byte vs 20 byte for sha1, and the truncation that would be
    * required would get rid of part of the little entropy we have.
    * */
   memset(uuid, 0, size);
   uint_uuid[0] = info->pci_domain;
   uint_uuid[1] = info->pci_bus;
   uint_uuid[2] = info->pci_dev;
   uint_uuid[3] = info->pci_func;
}

void ac_print_gpu_info(struct radeon_info *info, FILE *f)
{
   fprintf(f, "Device info:\n");
   fprintf(f, "    pci (domain:bus:dev.func): %04x:%02x:%02x.%x\n", info->pci_domain, info->pci_bus,
           info->pci_dev, info->pci_func);

   fprintf(f, "    name = %s\n", info->name);
   fprintf(f, "    marketing_name = %s\n", info->marketing_name);
   fprintf(f, "    is_pro_graphics = %u\n", info->is_pro_graphics);
   fprintf(f, "    pci_id = 0x%x\n", info->pci_id);
   fprintf(f, "    pci_rev_id = 0x%x\n", info->pci_rev_id);
   fprintf(f, "    family = %i\n", info->family);
   fprintf(f, "    chip_class = %i\n", info->chip_class);
   fprintf(f, "    family_id = %i\n", info->family_id);
   fprintf(f, "    chip_external_rev = %i\n", info->chip_external_rev);
   fprintf(f, "    clock_crystal_freq = %i\n", info->clock_crystal_freq);

   fprintf(f, "Features:\n");
   fprintf(f, "    has_graphics = %i\n", info->has_graphics);
   fprintf(f, "    num_rings[RING_GFX] = %i\n", info->num_rings[RING_GFX]);
   fprintf(f, "    num_rings[RING_DMA] = %i\n", info->num_rings[RING_DMA]);
   fprintf(f, "    num_rings[RING_COMPUTE] = %u\n", info->num_rings[RING_COMPUTE]);
   fprintf(f, "    num_rings[RING_UVD] = %i\n", info->num_rings[RING_UVD]);
   fprintf(f, "    num_rings[RING_VCE] = %i\n", info->num_rings[RING_VCE]);
   fprintf(f, "    num_rings[RING_UVD_ENC] = %i\n", info->num_rings[RING_UVD_ENC]);
   fprintf(f, "    num_rings[RING_VCN_DEC] = %i\n", info->num_rings[RING_VCN_DEC]);
   fprintf(f, "    num_rings[RING_VCN_ENC] = %i\n", info->num_rings[RING_VCN_ENC]);
   fprintf(f, "    num_rings[RING_VCN_JPEG] = %i\n", info->num_rings[RING_VCN_JPEG]);
   fprintf(f, "    has_clear_state = %u\n", info->has_clear_state);
   fprintf(f, "    has_distributed_tess = %u\n", info->has_distributed_tess);
   fprintf(f, "    has_dcc_constant_encode = %u\n", info->has_dcc_constant_encode);
   fprintf(f, "    has_rbplus = %u\n", info->has_rbplus);
   fprintf(f, "    rbplus_allowed = %u\n", info->rbplus_allowed);
   fprintf(f, "    has_load_ctx_reg_pkt = %u\n", info->has_load_ctx_reg_pkt);
   fprintf(f, "    has_out_of_order_rast = %u\n", info->has_out_of_order_rast);
   fprintf(f, "    cpdma_prefetch_writes_memory = %u\n", info->cpdma_prefetch_writes_memory);
   fprintf(f, "    has_gfx9_scissor_bug = %i\n", info->has_gfx9_scissor_bug);
   fprintf(f, "    has_tc_compat_zrange_bug = %i\n", info->has_tc_compat_zrange_bug);
   fprintf(f, "    has_msaa_sample_loc_bug = %i\n", info->has_msaa_sample_loc_bug);
   fprintf(f, "    has_ls_vgpr_init_bug = %i\n", info->has_ls_vgpr_init_bug);
   fprintf(f, "    has_32bit_predication = %i\n", info->has_32bit_predication);
   fprintf(f, "    has_3d_cube_border_color_mipmap = %i\n", info->has_3d_cube_border_color_mipmap);

   fprintf(f, "Display features:\n");
   fprintf(f, "    use_display_dcc_unaligned = %u\n", info->use_display_dcc_unaligned);
   fprintf(f, "    use_display_dcc_with_retile_blit = %u\n", info->use_display_dcc_with_retile_blit);

   fprintf(f, "Memory info:\n");
   fprintf(f, "    pte_fragment_size = %u\n", info->pte_fragment_size);
   fprintf(f, "    gart_page_size = %u\n", info->gart_page_size);
   fprintf(f, "    gart_size = %i MB\n", (int)DIV_ROUND_UP(info->gart_size, 1024 * 1024));
   fprintf(f, "    vram_size = %i MB\n", (int)DIV_ROUND_UP(info->vram_size, 1024 * 1024));
   fprintf(f, "    vram_vis_size = %i MB\n", (int)DIV_ROUND_UP(info->vram_vis_size, 1024 * 1024));
   fprintf(f, "    vram_type = %i\n", info->vram_type);
   fprintf(f, "    vram_bit_width = %i\n", info->vram_bit_width);
   fprintf(f, "    gds_size = %u kB\n", info->gds_size / 1024);
   fprintf(f, "    gds_gfx_partition_size = %u kB\n", info->gds_gfx_partition_size / 1024);
   fprintf(f, "    max_alloc_size = %i MB\n", (int)DIV_ROUND_UP(info->max_alloc_size, 1024 * 1024));
   fprintf(f, "    min_alloc_size = %u\n", info->min_alloc_size);
   fprintf(f, "    address32_hi = %u\n", info->address32_hi);
   fprintf(f, "    has_dedicated_vram = %u\n", info->has_dedicated_vram);
   fprintf(f, "    all_vram_visible = %u\n", info->all_vram_visible);
   fprintf(f, "    smart_access_memory = %u\n", info->smart_access_memory);
   fprintf(f, "    max_tcc_blocks = %i\n", info->max_tcc_blocks);
   fprintf(f, "    num_tcc_blocks = %i\n", info->num_tcc_blocks);
   fprintf(f, "    tcc_cache_line_size = %u\n", info->tcc_cache_line_size);
   fprintf(f, "    tcc_rb_non_coherent = %u\n", info->tcc_rb_non_coherent);
   fprintf(f, "    pc_lines = %u\n", info->pc_lines);
   fprintf(f, "    lds_size_per_workgroup = %u\n", info->lds_size_per_workgroup);
   fprintf(f, "    lds_alloc_granularity = %i\n", info->lds_alloc_granularity);
   fprintf(f, "    lds_encode_granularity = %i\n", info->lds_encode_granularity);
   fprintf(f, "    max_memory_clock = %i\n", info->max_memory_clock);
   fprintf(f, "    ce_ram_size = %i\n", info->ce_ram_size);
   fprintf(f, "    l1_cache_size = %i\n", info->l1_cache_size);
   fprintf(f, "    l2_cache_size = %i\n", info->l2_cache_size);

   fprintf(f, "CP info:\n");
   fprintf(f, "    gfx_ib_pad_with_type2 = %i\n", info->gfx_ib_pad_with_type2);
   fprintf(f, "    ib_alignment = %u\n", info->ib_alignment);
   fprintf(f, "    me_fw_version = %i\n", info->me_fw_version);
   fprintf(f, "    me_fw_feature = %i\n", info->me_fw_feature);
   fprintf(f, "    pfp_fw_version = %i\n", info->pfp_fw_version);
   fprintf(f, "    pfp_fw_feature = %i\n", info->pfp_fw_feature);
   fprintf(f, "    ce_fw_version = %i\n", info->ce_fw_version);
   fprintf(f, "    ce_fw_feature = %i\n", info->ce_fw_feature);

   fprintf(f, "Multimedia info:\n");
   fprintf(f, "    uvd_decode = %u\n", info->has_video_hw.uvd_decode);
   fprintf(f, "    vcn_decode = %u\n", info->has_video_hw.vcn_decode);
   fprintf(f, "    jpeg_decode = %u\n", info->has_video_hw.jpeg_decode);
   fprintf(f, "    vce_encode = %u\n", info->has_video_hw.vce_encode);
   fprintf(f, "    uvd_encode = %u\n", info->has_video_hw.uvd_encode);
   fprintf(f, "    vcn_encode = %u\n", info->has_video_hw.vcn_encode);
   fprintf(f, "    uvd_fw_version = %u\n", info->uvd_fw_version);
   fprintf(f, "    vce_fw_version = %u\n", info->vce_fw_version);
   fprintf(f, "    vce_harvest_config = %i\n", info->vce_harvest_config);

   fprintf(f, "Kernel & winsys capabilities:\n");
   fprintf(f, "    drm = %i.%i.%i\n", info->drm_major, info->drm_minor, info->drm_patchlevel);
   fprintf(f, "    has_userptr = %i\n", info->has_userptr);
   fprintf(f, "    has_syncobj = %u\n", info->has_syncobj);
   fprintf(f, "    has_timeline_syncobj = %u\n", info->has_timeline_syncobj);
   fprintf(f, "    has_fence_to_handle = %u\n", info->has_fence_to_handle);
   fprintf(f, "    has_local_buffers = %u\n", info->has_local_buffers);
   fprintf(f, "    kernel_flushes_hdp_before_ib = %u\n", info->kernel_flushes_hdp_before_ib);
   fprintf(f, "    htile_cmask_support_1d_tiling = %u\n", info->htile_cmask_support_1d_tiling);
   fprintf(f, "    si_TA_CS_BC_BASE_ADDR_allowed = %u\n", info->si_TA_CS_BC_BASE_ADDR_allowed);
   fprintf(f, "    has_bo_metadata = %u\n", info->has_bo_metadata);
   fprintf(f, "    has_gpu_reset_status_query = %u\n", info->has_gpu_reset_status_query);
   fprintf(f, "    has_eqaa_surface_allocator = %u\n", info->has_eqaa_surface_allocator);
   fprintf(f, "    has_format_bc1_through_bc7 = %u\n", info->has_format_bc1_through_bc7);
   fprintf(f, "    kernel_flushes_tc_l2_after_ib = %u\n", info->kernel_flushes_tc_l2_after_ib);
   fprintf(f, "    has_indirect_compute_dispatch = %u\n", info->has_indirect_compute_dispatch);
   fprintf(f, "    has_unaligned_shader_loads = %u\n", info->has_unaligned_shader_loads);
   fprintf(f, "    has_sparse_vm_mappings = %u\n", info->has_sparse_vm_mappings);
   fprintf(f, "    has_2d_tiling = %u\n", info->has_2d_tiling);
   fprintf(f, "    has_read_registers_query = %u\n", info->has_read_registers_query);
   fprintf(f, "    has_gds_ordered_append = %u\n", info->has_gds_ordered_append);
   fprintf(f, "    has_scheduled_fence_dependency = %u\n", info->has_scheduled_fence_dependency);
   fprintf(f, "    mid_command_buffer_preemption_enabled = %u\n",
           info->mid_command_buffer_preemption_enabled);
   fprintf(f, "    has_tmz_support = %u\n", info->has_tmz_support);

   fprintf(f, "Shader core info:\n");
   fprintf(f, "    max_shader_clock = %i\n", info->max_shader_clock);
   fprintf(f, "    num_good_compute_units = %i\n", info->num_good_compute_units);
   fprintf(f, "    max_good_cu_per_sa = %i\n", info->max_good_cu_per_sa);
   fprintf(f, "    min_good_cu_per_sa = %i\n", info->min_good_cu_per_sa);
   fprintf(f, "    max_se = %i\n", info->max_se);
   fprintf(f, "    num_se = %i\n", info->num_se);
   fprintf(f, "    max_sa_per_se = %i\n", info->max_sa_per_se);
   fprintf(f, "    max_wave64_per_simd = %i\n", info->max_wave64_per_simd);
   fprintf(f, "    num_physical_sgprs_per_simd = %i\n", info->num_physical_sgprs_per_simd);
   fprintf(f, "    num_physical_wave64_vgprs_per_simd = %i\n",
           info->num_physical_wave64_vgprs_per_simd);
   fprintf(f, "    num_simd_per_compute_unit = %i\n", info->num_simd_per_compute_unit);
   fprintf(f, "    min_sgpr_alloc = %i\n", info->min_sgpr_alloc);
   fprintf(f, "    max_sgpr_alloc = %i\n", info->max_sgpr_alloc);
   fprintf(f, "    sgpr_alloc_granularity = %i\n", info->sgpr_alloc_granularity);
   fprintf(f, "    min_wave64_vgpr_alloc = %i\n", info->min_wave64_vgpr_alloc);
   fprintf(f, "    max_vgpr_alloc = %i\n", info->max_vgpr_alloc);
   fprintf(f, "    wave64_vgpr_alloc_granularity = %i\n", info->wave64_vgpr_alloc_granularity);

   fprintf(f, "Render backend info:\n");
   fprintf(f, "    pa_sc_tile_steering_override = 0x%x\n", info->pa_sc_tile_steering_override);
   fprintf(f, "    max_render_backends = %i\n", info->max_render_backends);
   fprintf(f, "    num_tile_pipes = %i\n", info->num_tile_pipes);
   fprintf(f, "    pipe_interleave_bytes = %i\n", info->pipe_interleave_bytes);
   fprintf(f, "    enabled_rb_mask = 0x%x\n", info->enabled_rb_mask);
   fprintf(f, "    max_alignment = %u\n", (unsigned)info->max_alignment);
   fprintf(f, "    pbb_max_alloc_count = %u\n", info->pbb_max_alloc_count);

   fprintf(f, "GB_ADDR_CONFIG: 0x%08x\n", info->gb_addr_config);
   if (info->chip_class >= GFX10) {
      fprintf(f, "    num_pipes = %u\n", 1 << G_0098F8_NUM_PIPES(info->gb_addr_config));
      fprintf(f, "    pipe_interleave_size = %u\n",
              256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(info->gb_addr_config));
      fprintf(f, "    max_compressed_frags = %u\n",
              1 << G_0098F8_MAX_COMPRESSED_FRAGS(info->gb_addr_config));
      if (info->chip_class >= GFX10_3)
         fprintf(f, "    num_pkrs = %u\n", 1 << G_0098F8_NUM_PKRS(info->gb_addr_config));
   } else if (info->chip_class == GFX9) {
      fprintf(f, "    num_pipes = %u\n", 1 << G_0098F8_NUM_PIPES(info->gb_addr_config));
      fprintf(f, "    pipe_interleave_size = %u\n",
              256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(info->gb_addr_config));
      fprintf(f, "    max_compressed_frags = %u\n",
              1 << G_0098F8_MAX_COMPRESSED_FRAGS(info->gb_addr_config));
      fprintf(f, "    bank_interleave_size = %u\n",
              1 << G_0098F8_BANK_INTERLEAVE_SIZE(info->gb_addr_config));
      fprintf(f, "    num_banks = %u\n", 1 << G_0098F8_NUM_BANKS(info->gb_addr_config));
      fprintf(f, "    shader_engine_tile_size = %u\n",
              16 << G_0098F8_SHADER_ENGINE_TILE_SIZE(info->gb_addr_config));
      fprintf(f, "    num_shader_engines = %u\n",
              1 << G_0098F8_NUM_SHADER_ENGINES_GFX9(info->gb_addr_config));
      fprintf(f, "    num_gpus = %u (raw)\n", G_0098F8_NUM_GPUS_GFX9(info->gb_addr_config));
      fprintf(f, "    multi_gpu_tile_size = %u (raw)\n",
              G_0098F8_MULTI_GPU_TILE_SIZE(info->gb_addr_config));
      fprintf(f, "    num_rb_per_se = %u\n", 1 << G_0098F8_NUM_RB_PER_SE(info->gb_addr_config));
      fprintf(f, "    row_size = %u\n", 1024 << G_0098F8_ROW_SIZE(info->gb_addr_config));
      fprintf(f, "    num_lower_pipes = %u (raw)\n", G_0098F8_NUM_LOWER_PIPES(info->gb_addr_config));
      fprintf(f, "    se_enable = %u (raw)\n", G_0098F8_SE_ENABLE(info->gb_addr_config));
   } else {
      fprintf(f, "    num_pipes = %u\n", 1 << G_0098F8_NUM_PIPES(info->gb_addr_config));
      fprintf(f, "    pipe_interleave_size = %u\n",
              256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX6(info->gb_addr_config));
      fprintf(f, "    bank_interleave_size = %u\n",
              1 << G_0098F8_BANK_INTERLEAVE_SIZE(info->gb_addr_config));
      fprintf(f, "    num_shader_engines = %u\n",
              1 << G_0098F8_NUM_SHADER_ENGINES_GFX6(info->gb_addr_config));
      fprintf(f, "    shader_engine_tile_size = %u\n",
              16 << G_0098F8_SHADER_ENGINE_TILE_SIZE(info->gb_addr_config));
      fprintf(f, "    num_gpus = %u (raw)\n", G_0098F8_NUM_GPUS_GFX6(info->gb_addr_config));
      fprintf(f, "    multi_gpu_tile_size = %u (raw)\n",
              G_0098F8_MULTI_GPU_TILE_SIZE(info->gb_addr_config));
      fprintf(f, "    row_size = %u\n", 1024 << G_0098F8_ROW_SIZE(info->gb_addr_config));
      fprintf(f, "    num_lower_pipes = %u (raw)\n", G_0098F8_NUM_LOWER_PIPES(info->gb_addr_config));
   }
}

int ac_get_gs_table_depth(enum chip_class chip_class, enum radeon_family family)
{
   if (chip_class >= GFX9)
      return -1;

   switch (family) {
   case CHIP_OLAND:
   case CHIP_HAINAN:
   case CHIP_KAVERI:
   case CHIP_KABINI:
   case CHIP_ICELAND:
   case CHIP_CARRIZO:
   case CHIP_STONEY:
      return 16;
   case CHIP_TAHITI:
   case CHIP_PITCAIRN:
   case CHIP_VERDE:
   case CHIP_BONAIRE:
   case CHIP_HAWAII:
   case CHIP_TONGA:
   case CHIP_FIJI:
   case CHIP_POLARIS10:
   case CHIP_POLARIS11:
   case CHIP_POLARIS12:
   case CHIP_VEGAM:
      return 32;
   default:
      unreachable("Unknown GPU");
   }
}

void ac_get_raster_config(struct radeon_info *info, uint32_t *raster_config_p,
                          uint32_t *raster_config_1_p, uint32_t *se_tile_repeat_p)
{
   unsigned raster_config, raster_config_1, se_tile_repeat;

   switch (info->family) {
   /* 1 SE / 1 RB */
   case CHIP_HAINAN:
   case CHIP_KABINI:
   case CHIP_STONEY:
      raster_config = 0x00000000;
      raster_config_1 = 0x00000000;
      break;
   /* 1 SE / 4 RBs */
   case CHIP_VERDE:
      raster_config = 0x0000124a;
      raster_config_1 = 0x00000000;
      break;
   /* 1 SE / 2 RBs (Oland is special) */
   case CHIP_OLAND:
      raster_config = 0x00000082;
      raster_config_1 = 0x00000000;
      break;
   /* 1 SE / 2 RBs */
   case CHIP_KAVERI:
   case CHIP_ICELAND:
   case CHIP_CARRIZO:
      raster_config = 0x00000002;
      raster_config_1 = 0x00000000;
      break;
   /* 2 SEs / 4 RBs */
   case CHIP_BONAIRE:
   case CHIP_POLARIS11:
   case CHIP_POLARIS12:
      raster_config = 0x16000012;
      raster_config_1 = 0x00000000;
      break;
   /* 2 SEs / 8 RBs */
   case CHIP_TAHITI:
   case CHIP_PITCAIRN:
      raster_config = 0x2a00126a;
      raster_config_1 = 0x00000000;
      break;
   /* 4 SEs / 8 RBs */
   case CHIP_TONGA:
   case CHIP_POLARIS10:
      raster_config = 0x16000012;
      raster_config_1 = 0x0000002a;
      break;
   /* 4 SEs / 16 RBs */
   case CHIP_HAWAII:
   case CHIP_FIJI:
   case CHIP_VEGAM:
      raster_config = 0x3a00161a;
      raster_config_1 = 0x0000002e;
      break;
   default:
      fprintf(stderr, "ac: Unknown GPU, using 0 for raster_config\n");
      raster_config = 0x00000000;
      raster_config_1 = 0x00000000;
      break;
   }

   /* drm/radeon on Kaveri is buggy, so disable 1 RB to work around it.
    * This decreases performance by up to 50% when the RB is the bottleneck.
    */
   if (info->family == CHIP_KAVERI && !info->is_amdgpu)
      raster_config = 0x00000000;

   /* Fiji: Old kernels have incorrect tiling config. This decreases
    * RB performance by 25%. (it disables 1 RB in the second packer)
    */
   if (info->family == CHIP_FIJI && info->cik_macrotile_mode_array[0] == 0x000000e8) {
      raster_config = 0x16000012;
      raster_config_1 = 0x0000002a;
   }

   unsigned se_width = 8 << G_028350_SE_XSEL_GFX6(raster_config);
   unsigned se_height = 8 << G_028350_SE_YSEL_GFX6(raster_config);

   /* I don't know how to calculate this, though this is probably a good guess. */
   se_tile_repeat = MAX2(se_width, se_height) * info->max_se;

   *raster_config_p = raster_config;
   *raster_config_1_p = raster_config_1;
   if (se_tile_repeat_p)
      *se_tile_repeat_p = se_tile_repeat;
}

void ac_get_harvested_configs(struct radeon_info *info, unsigned raster_config,
                              unsigned *cik_raster_config_1_p, unsigned *raster_config_se)
{
   unsigned sh_per_se = MAX2(info->max_sa_per_se, 1);
   unsigned num_se = MAX2(info->max_se, 1);
   unsigned rb_mask = info->enabled_rb_mask;
   unsigned num_rb = MIN2(info->max_render_backends, 16);
   unsigned rb_per_pkr = MIN2(num_rb / num_se / sh_per_se, 2);
   unsigned rb_per_se = num_rb / num_se;
   unsigned se_mask[4];
   unsigned se;

   se_mask[0] = ((1 << rb_per_se) - 1) & rb_mask;
   se_mask[1] = (se_mask[0] << rb_per_se) & rb_mask;
   se_mask[2] = (se_mask[1] << rb_per_se) & rb_mask;
   se_mask[3] = (se_mask[2] << rb_per_se) & rb_mask;

   assert(num_se == 1 || num_se == 2 || num_se == 4);
   assert(sh_per_se == 1 || sh_per_se == 2);
   assert(rb_per_pkr == 1 || rb_per_pkr == 2);

   if (info->chip_class >= GFX7) {
      unsigned raster_config_1 = *cik_raster_config_1_p;
      if ((num_se > 2) && ((!se_mask[0] && !se_mask[1]) || (!se_mask[2] && !se_mask[3]))) {
         raster_config_1 &= C_028354_SE_PAIR_MAP;

         if (!se_mask[0] && !se_mask[1]) {
            raster_config_1 |= S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_3);
         } else {
            raster_config_1 |= S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_0);
         }
         *cik_raster_config_1_p = raster_config_1;
      }
   }

   for (se = 0; se < num_se; se++) {
      unsigned pkr0_mask = ((1 << rb_per_pkr) - 1) << (se * rb_per_se);
      unsigned pkr1_mask = pkr0_mask << rb_per_pkr;
      int idx = (se / 2) * 2;

      raster_config_se[se] = raster_config;
      if ((num_se > 1) && (!se_mask[idx] || !se_mask[idx + 1])) {
         raster_config_se[se] &= C_028350_SE_MAP;

         if (!se_mask[idx]) {
            raster_config_se[se] |= S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_3);
         } else {
            raster_config_se[se] |= S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_0);
         }
      }

      pkr0_mask &= rb_mask;
      pkr1_mask &= rb_mask;
      if (rb_per_se > 2 && (!pkr0_mask || !pkr1_mask)) {
         raster_config_se[se] &= C_028350_PKR_MAP;

         if (!pkr0_mask) {
            raster_config_se[se] |= S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_3);
         } else {
            raster_config_se[se] |= S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_0);
         }
      }

      if (rb_per_se >= 2) {
         unsigned rb0_mask = 1 << (se * rb_per_se);
         unsigned rb1_mask = rb0_mask << 1;

         rb0_mask &= rb_mask;
         rb1_mask &= rb_mask;
         if (!rb0_mask || !rb1_mask) {
            raster_config_se[se] &= C_028350_RB_MAP_PKR0;

            if (!rb0_mask) {
               raster_config_se[se] |= S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_3);
            } else {
               raster_config_se[se] |= S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_0);
            }
         }

         if (rb_per_se > 2) {
            rb0_mask = 1 << (se * rb_per_se + rb_per_pkr);
            rb1_mask = rb0_mask << 1;
            rb0_mask &= rb_mask;
            rb1_mask &= rb_mask;
            if (!rb0_mask || !rb1_mask) {
               raster_config_se[se] &= C_028350_RB_MAP_PKR1;

               if (!rb0_mask) {
                  raster_config_se[se] |= S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_3);
               } else {
                  raster_config_se[se] |= S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_0);
               }
            }
         }
      }
   }
}

unsigned ac_get_compute_resource_limits(struct radeon_info *info, unsigned waves_per_threadgroup,
                                        unsigned max_waves_per_sh, unsigned threadgroups_per_cu)
{
   unsigned compute_resource_limits = S_00B854_SIMD_DEST_CNTL(waves_per_threadgroup % 4 == 0);

   if (info->chip_class >= GFX7) {
      unsigned num_cu_per_se = info->num_good_compute_units / info->num_se;

      /* Force even distribution on all SIMDs in CU if the workgroup
       * size is 64. This has shown some good improvements if # of CUs
       * per SE is not a multiple of 4.
       */
      if (num_cu_per_se % 4 && waves_per_threadgroup == 1)
         compute_resource_limits |= S_00B854_FORCE_SIMD_DIST(1);

      assert(threadgroups_per_cu >= 1 && threadgroups_per_cu <= 8);
      compute_resource_limits |=
         S_00B854_WAVES_PER_SH(max_waves_per_sh) | S_00B854_CU_GROUP_COUNT(threadgroups_per_cu - 1);
   } else {
      /* GFX6 */
      if (max_waves_per_sh) {
         unsigned limit_div16 = DIV_ROUND_UP(max_waves_per_sh, 16);
         compute_resource_limits |= S_00B854_WAVES_PER_SH_GFX6(limit_div16);
      }
   }
   return compute_resource_limits;
}
