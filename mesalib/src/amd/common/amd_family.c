/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "amd_family.h"
#include "addrlib/src/amdgpu_asic_addr.h"
#include "util/macros.h"
#include "ac_gpu_info.h"

const char *ac_get_family_name(enum radeon_family family)
{
   switch (family) {
#define CASE(name) case CHIP_##name: return #name
   CASE(TAHITI);
   CASE(PITCAIRN);
   CASE(VERDE);
   CASE(OLAND);
   CASE(HAINAN);
   CASE(BONAIRE);
   CASE(KABINI);
   CASE(KAVERI);
   CASE(HAWAII);
   CASE(TONGA);
   CASE(ICELAND);
   CASE(CARRIZO);
   CASE(FIJI);
   CASE(STONEY);
   CASE(POLARIS10);
   CASE(POLARIS11);
   CASE(POLARIS12);
   CASE(VEGAM);
   CASE(VEGA10);
   CASE(RAVEN);
   CASE(VEGA12);
   CASE(VEGA20);
   CASE(RAVEN2);
   CASE(RENOIR);
   CASE(MI100);
   CASE(MI200);
   CASE(GFX940);
   CASE(NAVI10);
   CASE(NAVI12);
   CASE(NAVI14);
   CASE(NAVI21);
   CASE(NAVI22);
   CASE(NAVI23);
   CASE(VANGOGH);
   CASE(NAVI24);
   CASE(REMBRANDT);
   CASE(RAPHAEL_MENDOCINO);
   CASE(NAVI31);
   CASE(NAVI32);
   CASE(NAVI33);
   CASE(PHOENIX);
   CASE(PHOENIX2);
   CASE(GFX1150);
   CASE(GFX1151);
   CASE(GFX1152);
   CASE(GFX1153);
   CASE(GFX1200);
   CASE(GFX1201);
#undef CASE
   default:
      unreachable("Unknown GPU family");
   }
}

enum amd_gfx_level ac_get_gfx_level(enum radeon_family family)
{
   if (family >= CHIP_GFX1200)
      return GFX12;
   if (family >= CHIP_GFX1150)
      return GFX11_5;
   if (family >= CHIP_NAVI31)
      return GFX11;
   if (family >= CHIP_NAVI21)
      return GFX10_3;
   if (family >= CHIP_NAVI10)
      return GFX10;
   if (family >= CHIP_VEGA10)
      return GFX9;
   if (family >= CHIP_TONGA)
      return GFX8;
   if (family >= CHIP_BONAIRE)
      return GFX7;

   return GFX6;
}

const char *ac_get_llvm_processor_name(enum radeon_family family)
{
   switch (family) {
   case CHIP_TAHITI:
      return "tahiti";
   case CHIP_PITCAIRN:
      return "pitcairn";
   case CHIP_VERDE:
      return "verde";
   case CHIP_OLAND:
      return "oland";
   case CHIP_HAINAN:
      return "hainan";
   case CHIP_BONAIRE:
      return "bonaire";
   case CHIP_KABINI:
      return "kabini";
   case CHIP_KAVERI:
      return "kaveri";
   case CHIP_HAWAII:
      return "hawaii";
   case CHIP_TONGA:
      return "tonga";
   case CHIP_ICELAND:
      return "iceland";
   case CHIP_CARRIZO:
      return "carrizo";
   case CHIP_FIJI:
      return "fiji";
   case CHIP_STONEY:
      return "stoney";
   case CHIP_POLARIS10:
      return "polaris10";
   case CHIP_POLARIS11:
   case CHIP_POLARIS12:
   case CHIP_VEGAM:
      return "polaris11";
   case CHIP_VEGA10:
      return "gfx900";
   case CHIP_RAVEN:
      return "gfx902";
   case CHIP_VEGA12:
      return "gfx904";
   case CHIP_VEGA20:
      return "gfx906";
   case CHIP_RAVEN2:
   case CHIP_RENOIR:
      return "gfx909";
   case CHIP_MI100:
      return "gfx908";
   case CHIP_MI200:
      return "gfx90a";
   case CHIP_GFX940:
      return "gfx942";
   case CHIP_NAVI10:
      return "gfx1010";
   case CHIP_NAVI12:
      return "gfx1011";
   case CHIP_NAVI14:
      return "gfx1012";
   case CHIP_NAVI21:
      return "gfx1030";
   case CHIP_NAVI22:
      return "gfx1031";
   case CHIP_NAVI23:
      return "gfx1032";
   case CHIP_VANGOGH:
      return "gfx1033";
   case CHIP_NAVI24:
      return "gfx1034";
   case CHIP_REMBRANDT:
      return "gfx1035";
   case CHIP_RAPHAEL_MENDOCINO:
      return "gfx1036";
   case CHIP_NAVI31:
      return "gfx1100";
   case CHIP_NAVI32:
      return "gfx1101";
   case CHIP_NAVI33:
      return "gfx1102";
   case CHIP_PHOENIX:
   case CHIP_PHOENIX2:
      return "gfx1103";
   case CHIP_GFX1150:
      return "gfx1150";
   case CHIP_GFX1151:
      return "gfx1151";
   case CHIP_GFX1152:
      return "gfx1152";
   case CHIP_GFX1153:
      return "gfx1153";
   case CHIP_GFX1200:
      return "gfx1200";
   case CHIP_GFX1201:
      return "gfx1201";
   default:
      return "";
   }
}

const char *ac_get_ip_type_string(const struct radeon_info *info, enum amd_ip_type ip_type)
{
   switch (ip_type) {
   case AMD_IP_GFX:
      return "GFX";
   case AMD_IP_COMPUTE:
      return "COMPUTE";
   case AMD_IP_SDMA:
      return "SDMA";
   case AMD_IP_UVD:
      return "UVD";
   case AMD_IP_VCE:
      return "VCE";
   case AMD_IP_UVD_ENC:
      return "UVD_ENC";
   case AMD_IP_VCN_DEC:
      return "VCN_DEC";
   case AMD_IP_VCN_ENC: /* equal to AMD_IP_VCN_UNIFIED */
      return !info || info->vcn_ip_version >= VCN_4_0_0 ? "VCN" : "VCN_ENC";
   case AMD_IP_VCN_JPEG:
      return "VCN_JPEG";
   case AMD_IP_VPE:
      return "VPE";
   default:
      return "UNKNOWN_IP";
   }
}
