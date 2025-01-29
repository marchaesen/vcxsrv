/*
 * Copyright © 2021 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_SURFACE_TEST_COMMON_H
#define AC_SURFACE_TEST_COMMON_H

#include "ac_gpu_info.h"
#include "amdgfxregs.h"
#include "addrlib/src/amdgpu_asic_addr.h"

#ifdef _WIN32
#define AMDGPU_FAMILY_VI         130
#define AMDGPU_FAMILY_AI         141
#define AMDGPU_FAMILY_RV         142
#define AMDGPU_FAMILY_NV         143
#else
#include "drm-uapi/amdgpu_drm.h"
#endif

typedef void (*gpu_init_func)(struct radeon_info *info);

static void init_polaris12(struct radeon_info *info)
{
   info->family = CHIP_POLARIS12;
   info->gfx_level = GFX8;
   info->family_id = AMDGPU_FAMILY_VI;
   info->chip_external_rev = 0x64;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 64;
   info->max_render_backends = 4;

   uint32_t si_tile_mode_array[] = {
      0x00800150, 0x00800950, 0x00801150, 0x00801950, 0x00802950,
      0x00802948, 0x00802954, 0x00802954, 0x00000144, 0x02000148,
      0x02000150, 0x06000154, 0x06000154, 0x02400148, 0x02400150,
      0x02400170, 0x06400154, 0x06400154, 0x0040014c, 0x0100014c,
      0x0100015c, 0x01000174, 0x01000164, 0x01000164, 0x0040015c,
      0x01000160, 0x01000178, 0x02c00148, 0x02c00150, 0x06c00154,
      0x06c00154, 0x00000000
   };
   memcpy(info->si_tile_mode_array, si_tile_mode_array, sizeof(si_tile_mode_array));
   info->gb_addr_config = 0x22011002;
}

static void init_vega10(struct radeon_info *info)
{
   info->family = CHIP_VEGA10;
   info->gfx_level = GFX9;
   info->family_id = AMDGPU_FAMILY_AI;
   info->chip_external_rev = 0x01;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 64;
   info->max_render_backends = 16;

   info->gb_addr_config = 0x2a114042;
}

static void init_vega20(struct radeon_info *info)
{
   info->family = CHIP_VEGA20;
   info->gfx_level = GFX9;
   info->family_id = AMDGPU_FAMILY_AI;
   info->chip_external_rev = 0x30;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 64;
   info->max_render_backends = 16;

   info->gb_addr_config = 0x2a114042;
}


static void init_raven(struct radeon_info *info)
{
   info->family = CHIP_RAVEN;
   info->gfx_level = GFX9;
   info->family_id = AMDGPU_FAMILY_RV;
   info->chip_external_rev = 0x01;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = true;
   info->has_graphics = true;
   info->tcc_cache_line_size = 64;
   info->max_render_backends = 2;

   info->gb_addr_config = 0x24000042;
}

static void init_raven2(struct radeon_info *info)
{
   info->family = CHIP_RAVEN2;
   info->gfx_level = GFX9;
   info->family_id = AMDGPU_FAMILY_RV;
   info->chip_external_rev = 0x82;
   info->use_display_dcc_unaligned = true;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 64;
   info->max_render_backends = 1;

   info->gb_addr_config = 0x26013041;
}

static void init_navi10(struct radeon_info *info)
{
   info->family = CHIP_NAVI10;
   info->gfx_level = GFX10;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 3;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;

   info->gb_addr_config = 0x00100044;
}

static void init_navi14(struct radeon_info *info)
{
   info->family = CHIP_NAVI14;
   info->gfx_level = GFX10;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 0x15;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;

   info->gb_addr_config = 0x00000043;
}

static void init_gfx103(struct radeon_info *info)
{
   info->family = CHIP_NAVI21; /* This doesn't affect tests. */
   info->gfx_level = GFX10_3;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 0x28;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = true;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;
   info->has_rbplus = true;
   info->rbplus_allowed = true;

   info->gb_addr_config = 0x00000040; /* Other fields are set by test cases. */
}

static void init_gfx11(struct radeon_info *info)
{
   info->family = CHIP_NAVI31;
   info->gfx_level = GFX11;
   info->family_id = FAMILY_NV3;
   info->chip_external_rev = 0x01;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = true;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;
   info->has_rbplus = true;
   info->rbplus_allowed = true;

   info->gb_addr_config = 0x00000040; /* Other fields are set by test cases. */
}

static void init_gfx12(struct radeon_info *info)
{
   info->family = CHIP_GFX1200;
   info->gfx_level = GFX12;
   info->family_id = FAMILY_GFX12;
   info->chip_external_rev = 0x01;
   info->has_graphics = true;
   info->tcc_cache_line_size = 256;
   info->has_rbplus = true;
   info->rbplus_allowed = true;

   info->gb_addr_config = 0; /* Other fields are set by test cases. */
}

struct ac_fake_hw {
   const char *name;
   gpu_init_func init;
   int banks_or_pkrs;
   int pipes;
   int se;
   int rb_per_se;
};

static struct ac_fake_hw ac_fake_hw_db[] = {
   {"polaris12", init_polaris12},
   {"vega10", init_vega10, 4, 2, 2, 2},
   {"vega10_diff_bank", init_vega10, 3, 2, 2, 2},
   {"vega10_diff_rb", init_vega10, 4, 2, 2, 0},
   {"vega10_diff_pipe", init_vega10, 4, 0, 2, 2},
   {"vega10_diff_se", init_vega10, 4, 2, 1, 2},
   {"vega20", init_vega20, 4, 2, 2, 2},
   {"raven", init_raven, 0, 2, 0, 1},
   {"raven2", init_raven2, 3, 1, 0, 1},
   /* Just test a bunch of different numbers. (packers, pipes) */
   {"navi10", init_navi10, 0, 4},
   {"navi10_diff_pipe", init_navi10, 0, 3},
   {"navi10_diff_pkr", init_navi10, 1, 4},
   {"navi14", init_navi14, 1, 3},
   {"navi21", init_gfx103, 4, 4},
   {"navi21_8pkr", init_gfx103, 3, 4},
   {"navi22", init_gfx103, 3, 3},
   {"navi24", init_gfx103, 2, 2},
   {"vangogh", init_gfx103, 1, 2},
   {"vangogh_1pkr", init_gfx103, 0, 2},
   {"raphael", init_gfx103, 0, 1},
   {"navi31", init_gfx11, 5, 5},
   {"navi32", init_gfx11, 4, 4},
   {"navi33", init_gfx11, 3, 3},
   {"phoenix", init_gfx11, 2, 2},
   {"phoenix_2pkr", init_gfx11, 1, 2},
   {"phoenix2", init_gfx11, 0, 2},
   {"phoenix2_2pipe", init_gfx11, 0, 1},
   {"gfx12", init_gfx12, 4, 4},
};

static void get_radeon_info(struct radeon_info *info, struct ac_fake_hw *hw)
{
   if (info->drm_major != 3) {
      info->drm_major = 3;
      info->drm_minor = 30;
   }

   hw->init(info);

   switch(info->gfx_level) {
   case GFX9:
      info->gb_addr_config = (info->gb_addr_config &
                             C_0098F8_NUM_PIPES &
                             C_0098F8_NUM_BANKS &
                             C_0098F8_NUM_SHADER_ENGINES_GFX9 &
                             C_0098F8_NUM_RB_PER_SE) |
                             S_0098F8_NUM_PIPES(hw->pipes) |
                             S_0098F8_NUM_BANKS(hw->banks_or_pkrs) |
                             S_0098F8_NUM_SHADER_ENGINES_GFX9(hw->se) |
                             S_0098F8_NUM_RB_PER_SE(hw->rb_per_se);
      break;
   case GFX10:
   case GFX10_3:
   case GFX11:
   case GFX12:
      info->gb_addr_config = (info->gb_addr_config &
                             C_0098F8_NUM_PIPES &
                             C_0098F8_NUM_PKRS) |
                             S_0098F8_NUM_PIPES(hw->pipes) |
                             S_0098F8_NUM_PKRS(hw->banks_or_pkrs);
      /* 1 packer implies 1 RB except gfx10 where the field is ignored. */
      info->max_render_backends = info->gfx_level == GFX10 || hw->banks_or_pkrs ? 2 : 1;
      break;
   default:
      break;
   }
}

#endif
