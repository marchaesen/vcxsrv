/*
 * Copyright © 2021 Advanced Micro Devices, Inc.
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

#ifndef AC_SURFACE_TEST_COMMON_H
#define AC_SURFACE_TEST_COMMON_H

#include "ac_gpu_info.h"
#include "amdgfxregs.h"

typedef void (*gpu_init_func)(struct radeon_info *info);

static void init_vega10(struct radeon_info *info)
{
   info->family = CHIP_VEGA10;
   info->chip_class = GFX9;
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
   info->chip_class = GFX9;
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
   info->chip_class = GFX9;
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
   info->chip_class = GFX9;
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
   info->chip_class = GFX10;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 3;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;
   info->max_render_backends = 16;

   info->gb_addr_config = 0x00100044;
}

static void init_navi14(struct radeon_info *info)
{
   info->family = CHIP_NAVI14;
   info->chip_class = GFX10;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 0x15;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = false;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;
   info->max_render_backends = 8;

   info->gb_addr_config = 0x00000043;
}

static void init_sienna_cichlid(struct radeon_info *info)
{
   info->family = CHIP_SIENNA_CICHLID;
   info->chip_class = GFX10_3;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 0x28;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = true;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;
   info->has_rbplus = true;
   info->rbplus_allowed = true;
   info->max_render_backends = 16;

   info->gb_addr_config = 0x00000444;
}

static void init_navy_flounder(struct radeon_info *info)
{
   info->family = CHIP_NAVY_FLOUNDER;
   info->chip_class = GFX10_3;
   info->family_id = AMDGPU_FAMILY_NV;
   info->chip_external_rev = 0x32;
   info->use_display_dcc_unaligned = false;
   info->use_display_dcc_with_retile_blit = true;
   info->has_graphics = true;
   info->tcc_cache_line_size = 128;
   info->has_rbplus = true;
   info->rbplus_allowed = true;
   info->max_render_backends = 8;

   info->gb_addr_config = 0x00000344;
}

struct testcase {
   const char *name;
   gpu_init_func init;
   int banks_or_pkrs;
   int pipes;
   int se;
   int rb_per_se;
};

static struct testcase testcases[] = {
   {"vega10", init_vega10, 4, 2, 2, 2},
   {"vega10_diff_bank", init_vega10, 3, 2, 2, 2},
   {"vega10_diff_rb", init_vega10, 4, 2, 2, 0},
   {"vega10_diff_pipe", init_vega10, 4, 0, 2, 2},
   {"vega10_diff_se", init_vega10, 4, 2, 1, 2},
   {"vega20", init_vega20, 4, 2, 2, 2},
   {"raven", init_raven, 0, 2, 0, 1},
   {"raven2", init_raven2, 3, 1, 0, 1},
   {"navi10", init_navi10, 0, 4, 1, 0},
   {"navi10_diff_pipe", init_navi10, 0, 3, 1, 0},
   {"navi10_diff_pkr", init_navi10, 1, 4, 1, 0},
   {"navi14", init_navi14, 1, 3, 1, 0},
   {"sienna_cichlid", init_sienna_cichlid},
   {"navy_flounder", init_navy_flounder},
};

static struct radeon_info get_radeon_info(struct testcase *testcase)
{
   struct radeon_info info = {
      .drm_major = 3,
      .drm_minor = 30,
   };

   testcase->init(&info);

   switch(info.chip_class) {
   case GFX10_3:
      break;
   case GFX10:
      info.gb_addr_config = (info.gb_addr_config &
                             C_0098F8_NUM_PIPES &
                             C_0098F8_NUM_PKRS) |
                             S_0098F8_NUM_PIPES(testcase->pipes) |
                             S_0098F8_NUM_PKRS(testcase->banks_or_pkrs);
      break;
   case GFX9:
      info.gb_addr_config = (info.gb_addr_config &
                             C_0098F8_NUM_PIPES &
                             C_0098F8_NUM_BANKS &
                             C_0098F8_NUM_SHADER_ENGINES_GFX9 &
                             C_0098F8_NUM_RB_PER_SE) |
                             S_0098F8_NUM_PIPES(testcase->pipes) |
                             S_0098F8_NUM_BANKS(testcase->banks_or_pkrs) |
                             S_0098F8_NUM_SHADER_ENGINES_GFX9(testcase->se) |
                             S_0098F8_NUM_RB_PER_SE(testcase->rb_per_se);
      break;
   default:
      unreachable("Unhandled generation");
   }

   return info;
}

#endif
